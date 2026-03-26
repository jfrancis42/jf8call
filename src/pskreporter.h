#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call — JS8Call-compatible application
// Copyright (C) 2026 Ordo Artificum LLC
//
// PSKReporter spot submission via IPFIX/UDP to report.pskreporter.info:4739.
// Modeled after the JS8Call-improved PSKReporter implementation.

#include <QObject>
#include <QString>
#include <QDateTime>

class PskReporter : public QObject {
    Q_OBJECT
public:
    explicit PskReporter(QObject *parent = nullptr);
    ~PskReporter() override;

    // Set local station info (callsign, grid, software string).
    // Call whenever callsign or grid changes.
    void setLocalStation(const QString &callsign, const QString &grid,
                         const QString &softwareId);

    // Add a decoded spot. freqHz is the dial + audio offset in Hz.
    // Call once per decoded frame from a non-empty callsign.
    void addSpot(const QString &call, const QString &grid,
                 quint64 freqHz, int snrDb, const QDateTime &utc);

private slots:
    void onSendTimer();
    void onDescriptorTimer();

private:
    void reconnect();
    void sendReport(bool flush = false);
    void buildPreamble(QDataStream &out);

    static QString bandName(quint64 freqHz);

    struct Spot {
        QString  call;
        QString  grid;
        quint64  freqHz;
        int      snrDb;
        QDateTime utc;
    };

    // Socket and timers (owned as raw pointers on this object)
    class QUdpSocket *m_socket       = nullptr;
    class QTimer     *m_sendTimer    = nullptr;
    class QTimer     *m_descTimer    = nullptr;

    QString  m_rxCall;
    QString  m_rxGrid;
    QString  m_softwareId;

    QByteArray m_payload;
    QByteArray m_txData;
    QByteArray m_txResidue;

    QList<Spot>              m_spots;
    QHash<QString, qint64>   m_cache;   // key → unix-seconds

    quint32 m_observationId;
    quint32 m_seqNum        = 0;
    int     m_sendDescCount = 0;
    int     m_flushCounter  = 0;
    bool    m_started       = false;
};
