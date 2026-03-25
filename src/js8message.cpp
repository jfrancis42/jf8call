// SPDX-License-Identifier: GPL-3.0-or-later
#include "js8message.h"
#include "checksum.h"
#include "DecodedText.h"
#include <QStringList>
#include <QRegularExpression>
#include <cmath>

QString submodeName(int submodeEnum, int modemType)
{
    if (modemType == 1) {
        // Codec2 DATAC submode UI indices
        switch (submodeEnum) {
            case 0: return QStringLiteral("DATAC0");
            case 1: return QStringLiteral("DATAC1");
            case 2: return QStringLiteral("DATAC3");
            default: return QStringLiteral("?");
        }
    }
    // Gfsk8/JS8 bitmask values
    switch (submodeEnum) {
        case 0: return QStringLiteral("Normal");
        case 1: return QStringLiteral("Fast");
        case 2: return QStringLiteral("Turbo");
        case 4: return QStringLiteral("Slow");
        case 8: return QStringLiteral("Ultra");
        default: return QStringLiteral("?");
    }
}

bool JS8Message::isAddressedToMe(const QString &mycall) const
{
    if (mycall.isEmpty()) return false;
    const QString mc = mycall.toUpper();
    return to.toUpper() == mc;
}

JS8Message parseDecoded(const ModemDecoded &d,
                        const QString &rawTextHint,
                        const QString &mycall)
{
    JS8Message msg;
    msg.utc         = QDateTime::currentDateTimeUtc();
    msg.audioFreqHz = d.frequencyHz;
    msg.snrDb       = d.snrDb;
    msg.submodeEnum = d.submode;
    msg.submodeStr  = submodeName(d.submode, d.modemType);

    if (d.isRawText) {
        // Codec2 or other raw-text modem: message IS the human-readable text.
        // Try to parse "CALLSIGN: body" framing.
        msg.rawText = rawTextHint.isEmpty()
            ? QString::fromStdString(d.message)
            : rawTextHint;
        const int colon = msg.rawText.indexOf(QStringLiteral(": "));
        if (colon > 0 && colon <= 12) {
            msg.from = msg.rawText.left(colon).trimmed().toUpper();
            msg.body = msg.rawText.mid(colon + 2).trimmed();
        } else {
            msg.body = msg.rawText;
        }
        msg.type = JS8Message::Type::DirectedMessage;
        msg.grid = extractGrid(msg.rawText);
        Q_UNUSED(mycall)
        return msg;
    }

    // JS8 path: use DecodedText with the correct 3-arg constructor
    DecodedText dt(d.message, d.frameType, d.submode);

    // The human-readable message string
    msg.rawText = QString::fromStdString(dt.message()).trimmed();

    // Parse structured fields from directed message vector
    // directed_[0] = destination, directed_[1] = source, directed_[2+] = body words
    const auto &directed = dt.directedMessage();
    if (directed.size() >= 2) {
        msg.from = QString::fromStdString(directed[0]).trimmed().toUpper();
        msg.to   = QString::fromStdString(directed[1]).trimmed().toUpper();
        QStringList bodyParts;
        for (size_t i = 2; i < directed.size(); ++i)
            bodyParts.append(QString::fromStdString(directed[i]));
        msg.body = bodyParts.join(QStringLiteral(" ")).trimmed();
    } else if (!dt.compoundCall().empty()) {
        msg.from = QString::fromStdString(dt.compoundCall()).trimmed().toUpper();
        msg.body = QString::fromStdString(dt.extra()).trimmed();
    } else {
        msg.body = msg.rawText;
    }

    // Strip and verify CRC-16/KERMIT checksum if present.
    // Only attempt on directed messages (from + to both populated) where
    // the raw text is long enough to contain a real body + " XXX" suffix.
    if (!msg.from.isEmpty() && !msg.to.isEmpty() && !msg.rawText.isEmpty()) {
        QString rawStripped = msg.rawText;
        bool crcValid = false;
        if (JS8Checksum::tryStrip(rawStripped, crcValid)) {
            msg.hasChecksum  = true;
            msg.checksumValid = crcValid;
            msg.rawText = rawStripped;
            // Also strip from body (last word of body is the checksum)
            if (!msg.body.isEmpty()) {
                QString bodyStripped = msg.body;
                bool unused = false;
                JS8Checksum::tryStrip(bodyStripped, unused);
                msg.body = bodyStripped;
            }
        }
    }

    // Classify message type
    const QString bodyUpper = msg.body.toUpper();
    const QString toUpper   = msg.to.toUpper();

    if (dt.isHeartbeat() || bodyUpper.contains(QLatin1String("@HB"))) {
        msg.type = JS8Message::Type::Heartbeat;
        if (msg.to.isEmpty()) msg.to = QStringLiteral("@HB");
    } else if (bodyUpper == QLatin1String("@SNR?") ||
               msg.rawText.contains(QLatin1String("@SNR?"))) {
        msg.type = JS8Message::Type::SnrQuery;
    } else if (bodyUpper.startsWith(QLatin1String("SNR ")) ||
               bodyUpper.contains(QLatin1String(" SNR "))) {
        msg.type = JS8Message::Type::SnrReply;
    } else if (bodyUpper == QLatin1String("@INFO?") ||
               msg.rawText.contains(QLatin1String("@INFO?"))) {
        msg.type = JS8Message::Type::InfoQuery;
    } else if (bodyUpper.startsWith(QLatin1String("INFO "))) {
        msg.type = JS8Message::Type::InfoReply;
    } else if (bodyUpper == QLatin1String("@?") ||
               msg.rawText.contains(QLatin1String(" @?")) ||
               bodyUpper == QLatin1String("@STATUS?") ||
               msg.rawText.contains(QLatin1String("@STATUS?"))) {
        msg.type = JS8Message::Type::StatusQuery;
    } else if (bodyUpper == QLatin1String("HEARD") ||
               bodyUpper.startsWith(QLatin1String("STATUS "))) {
        msg.type = JS8Message::Type::StatusReply;
    } else if (bodyUpper == QLatin1String("@GRID?") ||
               msg.rawText.contains(QLatin1String("@GRID?"))) {
        msg.type = JS8Message::Type::GridQuery;
    } else if (bodyUpper.startsWith(QLatin1String("GRID "))) {
        msg.type = JS8Message::Type::GridReply;
    } else if (bodyUpper == QLatin1String("@HEARING?") ||
               msg.rawText.contains(QLatin1String("@HEARING?"))) {
        msg.type = JS8Message::Type::HearingQuery;
    } else if (bodyUpper.startsWith(QLatin1String("HEARING "))) {
        msg.type = JS8Message::Type::HearingReply;
    } else if (bodyUpper == QLatin1String("ACK")) {
        msg.type = JS8Message::Type::AckMessage;
    } else if (bodyUpper.startsWith(QLatin1String("MSG "))) {
        msg.type = JS8Message::Type::MsgCommand;
    } else if (bodyUpper == QLatin1String("QUERY MSGS")) {
        msg.type = JS8Message::Type::QueryMsgs;
    } else if (bodyUpper.startsWith(QLatin1String("QUERY MSG "))) {
        msg.type = JS8Message::Type::QueryMsg;
    } else if (bodyUpper == QLatin1String("NO")) {
        msg.type = JS8Message::Type::MsgNotAvailable;
    } else if (bodyUpper.startsWith(QLatin1String("YES MSG ID "))) {
        msg.type = JS8Message::Type::MsgAvailable;
    } else if (bodyUpper.startsWith(QLatin1String("MSG ")) &&
               bodyUpper.contains(QLatin1String(" FROM "))) {
        msg.type = JS8Message::Type::MsgDelivery;
    } else if (dt.isDirectedMessage()) {
        msg.type = JS8Message::Type::DirectedMessage;
    }

    // Extract grid from raw text (present in heartbeats: "W5XYZ DM79AA @HB")
    msg.grid = extractGrid(msg.rawText);

    Q_UNUSED(mycall)
    return msg;
}

// ── Maidenhead / geodesy utilities ────────────────────────────────────────────

QString extractGrid(const QString &text)
{
    // Match 4 or 6-character Maidenhead locator
    static const QRegularExpression re(
        QStringLiteral("\\b([A-Ra-r]{2}[0-9]{2}(?:[A-Xa-x]{2})?)\\b"));
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}

bool gridToLatLon(const QString &grid, double &lat, double &lon)
{
    if (grid.size() < 4) return false;
    const QString g = grid.toUpper();
    if (g[0] < QLatin1Char('A') || g[0] > QLatin1Char('R')) return false;
    if (g[1] < QLatin1Char('A') || g[1] > QLatin1Char('R')) return false;
    if (!g[2].isDigit() || !g[3].isDigit()) return false;

    lon = (g[0].unicode() - 'A') * 20.0 - 180.0;
    lat = (g[1].unicode() - 'A') * 10.0 - 90.0;
    lon += g[2].digitValue() * 2.0;
    lat += g[3].digitValue() * 1.0;

    if (g.size() >= 6 &&
        g[4] >= QLatin1Char('A') && g[4] <= QLatin1Char('X') &&
        g[5] >= QLatin1Char('A') && g[5] <= QLatin1Char('X')) {
        lon += (g[4].unicode() - 'A') * (5.0 / 60.0);
        lat += (g[5].unicode() - 'A') * (2.5 / 60.0);
        lon += 2.5 / 60.0;    // centre of subsquare
        lat += 1.25 / 60.0;
    } else {
        lon += 1.0;            // centre of grid square
        lat += 0.5;
    }
    return true;
}

bool calcDistBearing(const QString &myGrid, const QString &theirGrid,
                     double &distKm, double &bearingDeg)
{
    double lat1, lon1, lat2, lon2;
    if (!gridToLatLon(myGrid, lat1, lon1))    return false;
    if (!gridToLatLon(theirGrid, lat2, lon2)) return false;

    constexpr double R    = 6371.0;
    constexpr double kPi  = M_PI;
    const double dLat = (lat2 - lat1) * kPi / 180.0;
    const double dLon = (lon2 - lon1) * kPi / 180.0;
    const double rl1  = lat1 * kPi / 180.0;
    const double rl2  = lat2 * kPi / 180.0;

    const double a = std::sin(dLat / 2) * std::sin(dLat / 2)
                   + std::cos(rl1) * std::cos(rl2)
                   * std::sin(dLon / 2) * std::sin(dLon / 2);
    distKm = R * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    const double y = std::sin(dLon) * std::cos(rl2);
    const double x = std::cos(rl1) * std::sin(rl2)
                   - std::sin(rl1) * std::cos(rl2) * std::cos(dLon);
    bearingDeg = std::fmod(std::atan2(y, x) * 180.0 / kPi + 360.0, 360.0);
    return true;
}
