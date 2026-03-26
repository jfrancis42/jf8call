// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call — JS8Call-compatible application
// Copyright (C) 2026 Ordo Artificum LLC
//
// PSKReporter IPFIX/UDP spot submission.
// Protocol: https://pskreporter.info/pskdev.html
// Modeled after the JS8Call-improved PSKReporter by PY2SDR, G4WJS, W6BAZ, K4RWR.

#include "pskreporter.h"

#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QRandomGenerator>
#include <QString>
#include <QTimer>
#include <QUdpSocket>

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr const char *kHost          = "report.pskreporter.info";
static constexpr quint16     kPort          = 4739;
static constexpr int         kSendIntervalS = 120;   // seconds between batches
static constexpr int         kJitterMaxS    = 5;     // random jitter on top
static constexpr int         kFlushEvery    = 4;     // flush residue every N intervals
static constexpr qint64      kCacheTimeout  = 3600;  // seconds per call+band
static constexpr qsizetype   kMaxStringLen  = 254;
static constexpr int         kMinPayload    = 508;
static constexpr int         kMaxPayload    = 10000;
static constexpr quint32     kEnterprise    = 30351u;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void writeUtfString(QDataStream &out, const QString &s)
{
    QByteArray utf = s.toUtf8();
    if (utf.size() > kMaxStringLen) {
        // Walk back to a UTF-8 codepoint boundary to avoid truncating mid-sequence.
        qsizetype cut = kMaxStringLen;
        while (cut > 0 && (static_cast<unsigned char>(utf.at(cut)) & 0xC0u) == 0x80u)
            --cut;
        utf.truncate(cut);
    }
    out << quint8(utf.size());
    out.writeRawData(utf.constData(), utf.size());
}

static qsizetype padBytes(qsizetype n)
{
    return ((n + 3) & ~3) - n;
}

static void setLength(QDataStream &out, const QByteArray &buf)
{
    if (auto pad = padBytes(buf.size()); pad)
        out.writeRawData(QByteArray(pad, '\0').constData(), pad);
    auto pos = out.device()->pos();
    out.device()->seek(sizeof(quint16));          // skip first 16-bit field
    out << static_cast<quint16>(buf.size());
    out.device()->seek(pos);
}

// Sender Information Descriptor (template for spot records).
static void appendSID(QDataStream &msg)
{
    QByteArray buf;
    QDataStream s{&buf, QIODevice::WriteOnly};
    s << quint16(2u)              // Template Set ID
      << quint16(0u)              // Length placeholder
      << quint16(0x50e3u)         // Link ID (sender template)
      << quint16(7u)              // Field count
      // 1: senderCallsign (variable)
      << quint16(0x8000u + 1u) << quint16(0xffffu) << quint32(kEnterprise)
      // 2: frequency (5 bytes fixed)
      << quint16(0x8000u + 5u) << quint16(5u) << quint32(kEnterprise)
      // 3: sNR (1 byte signed)
      << quint16(0x8000u + 6u) << quint16(1u) << quint32(kEnterprise)
      // 4: mode (variable)
      << quint16(0x8000u + 10u) << quint16(0xffffu) << quint32(kEnterprise)
      // 5: senderLocator (variable)
      << quint16(0x8000u + 3u) << quint16(0xffffu) << quint32(kEnterprise)
      // 6: informationSource (1 byte)
      << quint16(0x8000u + 11u) << quint16(1u) << quint32(kEnterprise)
      // 7: dateTimeSeconds (4 bytes)
      << quint16(150u) << quint16(4u);
    setLength(s, buf);
    msg.writeRawData(buf.constData(), buf.size());
}

// Receiver Information Descriptor (template for reporter record).
static void appendRID(QDataStream &msg)
{
    QByteArray buf;
    QDataStream s{&buf, QIODevice::WriteOnly};
    s << quint16(3u)              // Options Template Set ID
      << quint16(0u)              // Length placeholder
      << quint16(0x50e2u)         // Link ID (receiver template)
      << quint16(4u)              // Field count
      << quint16(0u)              // Scope field count
      // 1: receiverCallsign (variable)
      << quint16(0x8000u + 2u) << quint16(0xffffu) << quint32(kEnterprise)
      // 2: receiverLocator (variable)
      << quint16(0x8000u + 4u) << quint16(0xffffu) << quint32(kEnterprise)
      // 3: decodingSoftware (variable)
      << quint16(0x8000u + 8u) << quint16(0xffffu) << quint32(kEnterprise)
      // 4: antennaInformation (variable)
      << quint16(0x8000u + 9u) << quint16(0xffffu) << quint32(kEnterprise);
    setLength(s, buf);
    msg.writeRawData(buf.constData(), buf.size());
}

// ── PskReporter ───────────────────────────────────────────────────────────────

PskReporter::PskReporter(QObject *parent)
    : QObject(parent)
    , m_observationId(QRandomGenerator::global()->generate())
{
    m_sendTimer = new QTimer(this);
    m_sendTimer->setSingleShot(false);
    connect(m_sendTimer, &QTimer::timeout, this, &PskReporter::onSendTimer);

    m_descTimer = new QTimer(this);
    m_descTimer->setSingleShot(false);
    connect(m_descTimer, &QTimer::timeout, this, &PskReporter::onDescriptorTimer);
}

PskReporter::~PskReporter()
{
    if (m_socket) {
        sendReport(true);
        m_socket->close();
    }
}

void PskReporter::setLocalStation(const QString &callsign, const QString &grid,
                                   const QString &softwareId)
{
    m_rxCall      = callsign;
    m_rxGrid      = grid;
    m_softwareId  = softwareId;

    if (!m_started && !callsign.isEmpty()) {
        m_started = true;
        reconnect();
    }
}

void PskReporter::addSpot(const QString &call, const QString &grid,
                           quint64 freqHz, int snrDb, const QDateTime &utc)
{
    if (!m_started) return;
    if (call.trimmed().isEmpty()) return;

    const QString key = call + QLatin1Char('_') + bandName(freqHz);
    const qint64  now = QDateTime::currentSecsSinceEpoch();

    auto it = m_cache.find(key);
    if (it == m_cache.end() || (now - it.value()) >= kCacheTimeout) {
        // New or expired: add to queue, update cache
        m_spots.append({call, grid, freqHz, snrDb, utc});
        m_cache[key] = now;
    } else {
        // Within cache window: update the most recent queued spot for this key
        for (int i = m_spots.size() - 1; i >= 0; --i) {
            if (m_spots[i].call == call && bandName(m_spots[i].freqHz) == bandName(freqHz)) {
                m_spots[i] = {call, grid, freqHz, snrDb, utc};
                m_cache[key] = now;
                break;
            }
        }
    }

    // Prune stale cache entries (> 2× timeout)
    for (auto cit = m_cache.begin(); cit != m_cache.end(); ) {
        if (now - cit.value() > kCacheTimeout * 2)
            cit = m_cache.erase(cit);
        else
            ++cit;
    }
}

void PskReporter::reconnect()
{
    delete m_socket;
    m_socket = new QUdpSocket(this);
    m_sendDescCount = 3;   // send templates 3 times after reconnect

    m_socket->connectToHost(QString::fromLatin1(kHost), kPort,
                            QAbstractSocket::WriteOnly);

    if (!m_sendTimer->isActive()) {
        int interval = kSendIntervalS + QRandomGenerator::global()->bounded(kJitterMaxS + 1);
        m_sendTimer->start(interval * 1000);
    }
    if (!m_descTimer->isActive()) {
        m_descTimer->start(3600 * 1000);   // re-send templates hourly
    }
}

void PskReporter::onSendTimer()
{
    ++m_flushCounter;
    sendReport((m_flushCounter % kFlushEvery) == 0);
}

void PskReporter::onDescriptorTimer()
{
    m_sendDescCount = 3;
}

void PskReporter::buildPreamble(QDataStream &out)
{
    out << quint16(10u)   // IPFIX version
        << quint16(0u)    // Length placeholder
        << quint32(0u)    // Export time placeholder
        << ++m_seqNum
        << m_observationId;

    if (m_sendDescCount > 0) {
        --m_sendDescCount;
        appendSID(out);
        appendRID(out);
    }

    // Receiver information record (sent every time)
    QByteArray rec;
    QDataStream rs{&rec, QIODevice::WriteOnly};
    rs << quint16(0x50e2u) << quint16(0u);  // template ID + length placeholder
    writeUtfString(rs, m_rxCall);
    writeUtfString(rs, m_rxGrid);
    writeUtfString(rs, m_softwareId);
    writeUtfString(rs, QString());           // antenna info (empty)
    setLength(rs, rec);
    out.writeRawData(rec.constData(), rec.size());
}

void PskReporter::sendReport(bool flush)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    QDataStream msg{&m_payload, QIODevice::WriteOnly | QIODevice::Append};
    QDataStream tx{&m_txData,   QIODevice::WriteOnly | QIODevice::Append};

    if (m_payload.isEmpty())
        buildPreamble(msg);

    bool doFlush = flush;
    while (!m_spots.isEmpty() || doFlush) {
        if (m_payload.isEmpty())
            buildPreamble(msg);

        if (m_txData.isEmpty() && (!m_spots.isEmpty() || !m_txResidue.isEmpty())) {
            tx << quint16(0x50e3u) << quint16(0u);  // sender template ID + length
        }

        if (!m_txResidue.isEmpty()) {
            tx.writeRawData(m_txResidue.constData(), m_txResidue.size());
            m_txResidue.clear();
        }

        int prevTxSize = m_txData.size();
        while (!m_spots.isEmpty() || doFlush) {
            prevTxSize = m_txData.size();

            if (!m_spots.isEmpty()) {
                const Spot spot = m_spots.takeFirst();
                writeUtfString(tx, spot.call);
                // Frequency: 5 bytes big-endian
                tx << quint8(spot.freqHz >> 32)
                   << quint8(spot.freqHz >> 24)
                   << quint8(spot.freqHz >> 16)
                   << quint8(spot.freqHz >>  8)
                   << quint8(spot.freqHz)
                   << qint8(spot.snrDb);
                writeUtfString(tx, QStringLiteral("JS8"));
                writeUtfString(tx, spot.grid);
                tx << quint8(1u)   // REPORTER_SOURCE_AUTOMATIC
                   << static_cast<quint32>(spot.utc.toSecsSinceEpoch());
            }

            qsizetype total = m_payload.size() + m_txData.size();
            total += padBytes(m_txData.size());
            total += padBytes(total);

            const bool overMax   = (total > kMaxPayload);
            const bool aboveMin  = (m_spots.isEmpty() && total > kMinPayload);
            const bool doneFlush = (doFlush && m_spots.isEmpty());

            if (overMax || aboveMin || doneFlush) {
                if (!m_txData.isEmpty()) {
                    qsizetype sendSize = (total <= kMaxPayload)
                                        ? m_txData.size() : prevTxSize;
                    QByteArray txChunk = m_txData.left(sendSize);
                    QDataStream cs{&txChunk, QIODevice::WriteOnly | QIODevice::Append};
                    setLength(cs, txChunk);
                    msg.writeRawData(txChunk.constData(), txChunk.size());
                }

                // Finalize message: fill in length and export time
                setLength(msg, m_payload);
                msg.device()->seek(2 * sizeof(quint16));
                msg << static_cast<quint32>(QDateTime::currentSecsSinceEpoch());

                m_socket->write(m_payload);

                doFlush = false;
                msg.device()->seek(0);
                m_payload.clear();

                m_txResidue = m_txData.mid(prevTxSize);
                tx.device()->seek(0);
                m_txData.clear();
                break;
            }
        }
    }
}

// Map frequency in Hz to amateur band name for cache key dedup.
// Returns "unknown" for out-of-band frequencies.
QString PskReporter::bandName(quint64 freqHz)
{
    struct Band { quint64 lo; quint64 hi; const char *name; };
    static const Band bands[] = {
        { 1'800'000,   2'000'000, "160m" },
        { 3'500'000,   4'000'000,  "80m" },
        { 5'000'000,   5'500'000,  "60m" },
        { 7'000'000,   7'300'000,  "40m" },
        {10'100'000,  10'150'000,  "30m" },
        {14'000'000,  14'350'000,  "20m" },
        {18'068'000,  18'168'000,  "17m" },
        {21'000'000,  21'450'000,  "15m" },
        {24'890'000,  24'990'000,  "12m" },
        {28'000'000,  29'700'000,  "10m" },
        {50'000'000,  54'000'000,   "6m" },
        {144'000'000,148'000'000,   "2m" },
    };
    for (const auto &b : bands)
        if (freqHz >= b.lo && freqHz < b.hi)
            return QString::fromLatin1(b.name);
    return QStringLiteral("unknown");
}
