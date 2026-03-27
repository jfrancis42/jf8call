#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Solar / geomagnetic data fetcher.
// Fetches from NOAA SWPC every 15 minutes; emits updated() on success.

#include <QObject>
#include <QDateTime>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

struct SolarData {
    int     sfi       = -1;    // Solar Flux Index (10.7 cm), SFU; -1 = unknown
    int     ssn       = -1;    // Smoothed sunspot number; -1 = unknown
    double  aIndex    = -1.0;  // Geomagnetic A-index (0-400); -1 = unknown
    double  kIndex    = -1.0;  // Geomagnetic K-index (0-9); -1 = unknown
    QString xrayClass;         // X-ray background flux class (e.g. "B1.5")
    int     gScale    = 0;     // NOAA G-scale (0-5, 0=none)
    int     sScale    = 0;     // NOAA S-scale (0-5, 0=none)
    int     rScale    = 0;     // NOAA R-scale (0-5, 0=none)
    QDateTime lastUpdate;
    bool    valid     = false;

    // Short description of propagation conditions based on SFI / K-index
    QString propagationSummary() const;
    QString kIndexStr()   const;
    QString aIndexStr()   const;
    QString gScaleStr()   const;
    QString rScaleStr()   const;
};

class SolarDataFetcher : public QObject {
    Q_OBJECT
public:
    explicit SolarDataFetcher(QObject *parent = nullptr);

    const SolarData &data() const { return m_data; }
    void fetchNow();

signals:
    void updated(const SolarData &data);
    void fetchError(const QString &msg);

private slots:
    void onScalesReply(QNetworkReply *reply);
    void onFluxReply(QNetworkReply *reply);
    void onKpReply(QNetworkReply *reply);

private:
    void maybeEmitUpdate();

    QNetworkAccessManager *m_nam;
    QTimer                *m_timer;
    SolarData              m_data;
    bool                   m_scalesDone = false;
    bool                   m_fluxDone   = false;
    bool                   m_kpDone     = false;

    // Partial results while requests are in flight
    int     m_pendingSfi    = -1;
    double  m_pendingAIndex = -1.0;
    double  m_pendingKIndex = -1.0;
    int     m_pendingG      = 0;
    int     m_pendingS      = 0;
    int     m_pendingR      = 0;
};
