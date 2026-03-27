// SPDX-License-Identifier: GPL-3.0-or-later
#include "solardata.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>

// ── SolarData helpers ────────────────────────────────────────────────────────

QString SolarData::propagationSummary() const
{
    if (!valid) return QStringLiteral("No data");
    QString s;
    if (sfi >= 150)      s = QStringLiteral("Excellent");
    else if (sfi >= 120) s = QStringLiteral("Very Good");
    else if (sfi >= 100) s = QStringLiteral("Good");
    else if (sfi >= 80)  s = QStringLiteral("Fair");
    else if (sfi > 0)    s = QStringLiteral("Poor");
    else                 s = QStringLiteral("Unknown");
    if (kIndex >= 5)      s += QStringLiteral(" (storm)");
    else if (kIndex >= 4) s += QStringLiteral(" (unsettled)");
    return s;
}

QString SolarData::kIndexStr() const
{
    return (kIndex >= 0) ? QString::number(kIndex, 'f', 1) : QStringLiteral("--");
}

QString SolarData::aIndexStr() const
{
    return (aIndex >= 0) ? QString::number(static_cast<int>(aIndex)) : QStringLiteral("--");
}

QString SolarData::gScaleStr() const
{
    if (gScale <= 0) return QStringLiteral("G0");
    return QStringLiteral("G%1").arg(gScale);
}

QString SolarData::rScaleStr() const
{
    if (rScale <= 0) return QStringLiteral("R0");
    return QStringLiteral("R%1").arg(rScale);
}

// ── SolarDataFetcher ─────────────────────────────────────────────────────────

SolarDataFetcher::SolarDataFetcher(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(15 * 60 * 1000);  // 15 minutes
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &SolarDataFetcher::fetchNow);
    m_timer->start();

    // Initial fetch after a short delay (let app start first)
    QTimer::singleShot(3000, this, &SolarDataFetcher::fetchNow);
}

void SolarDataFetcher::fetchNow()
{
    m_scalesDone = false;
    m_fluxDone   = false;
    m_kpDone     = false;
    m_pendingSfi    = -1;
    m_pendingAIndex = -1.0;
    m_pendingKIndex = -1.0;
    m_pendingG = m_pendingS = m_pendingR = 0;

    // Request 1: NOAA G/S/R scales
    {
        QNetworkRequest req;
        req.setUrl(QUrl(QStringLiteral("https://services.swpc.noaa.gov/products/noaa-scales.json")));
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            onScalesReply(reply); reply->deleteLater();
        });
    }

    // Request 2: F10.7 cm solar flux index (SFI)
    // Returns: {"Flux":"156","TimeStamp":"..."}
    {
        QNetworkRequest req;
        req.setUrl(QUrl(QStringLiteral("https://services.swpc.noaa.gov/products/summary/10cm-flux.json")));
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            onFluxReply(reply); reply->deleteLater();
        });
    }

    // Request 3: Planetary K-index (3-hourly; most recent row last)
    // Columns: ["time_tag","Kp","a_running","station_count"]
    {
        QNetworkRequest req;
        req.setUrl(QUrl(QStringLiteral("https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json")));
        req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            onKpReply(reply); reply->deleteLater();
        });
    }
}

// Parse noaa-scales.json: {"0":{"G":{"Scale":"0",...},"S":{...},"R":{...}}, ...}
void SolarDataFetcher::onScalesReply(QNetworkReply *reply)
{
    m_scalesDone = true;
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "SolarDataFetcher: scales error:" << reply->errorString();
        maybeEmitUpdate();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) { maybeEmitUpdate(); return; }

    const QJsonObject cur = doc.object().value(QStringLiteral("0")).toObject();
    if (cur.isEmpty()) { maybeEmitUpdate(); return; }

    m_pendingG = cur.value(QStringLiteral("G")).toObject()
                    .value(QStringLiteral("Scale")).toString().toInt();
    m_pendingS = cur.value(QStringLiteral("S")).toObject()
                    .value(QStringLiteral("Scale")).toString().toInt();
    m_pendingR = cur.value(QStringLiteral("R")).toObject()
                    .value(QStringLiteral("Scale")).toString().toInt();

    maybeEmitUpdate();
}

// Parse 10cm-flux.json: {"Flux":"156","TimeStamp":"..."}
void SolarDataFetcher::onFluxReply(QNetworkReply *reply)
{
    m_fluxDone = true;
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "SolarDataFetcher: flux error:" << reply->errorString();
        maybeEmitUpdate();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) { maybeEmitUpdate(); return; }

    const QJsonObject obj = doc.object();
    const QJsonValue fv = obj.value(QStringLiteral("Flux"));
    if (fv.isString())
        m_pendingSfi = fv.toString().toInt();
    else if (fv.isDouble())
        m_pendingSfi = static_cast<int>(fv.toDouble());

    maybeEmitUpdate();
}

// Parse noaa-planetary-k-index.json: array of arrays
// Row 0: ["time_tag","Kp","a_running","station_count"]
// Last data row: ["2026-03-27 09:00:00.000","1.67","6","8"]
void SolarDataFetcher::onKpReply(QNetworkReply *reply)
{
    m_kpDone = true;
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "SolarDataFetcher: kp error:" << reply->errorString();
        maybeEmitUpdate();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray()) { maybeEmitUpdate(); return; }

    const QJsonArray arr = doc.array();
    // Row 0 is the header; last row is most recent data
    if (arr.size() < 2) { maybeEmitUpdate(); return; }

    const QJsonArray last = arr.last().toArray();
    // ["time_tag", "Kp", "a_running", "station_count"]
    if (last.size() >= 3) {
        bool ok;
        double kp = last[1].toString().toDouble(&ok);
        if (ok) m_pendingKIndex = kp;
        double ap = last[2].toString().toDouble(&ok);
        if (ok) m_pendingAIndex = ap;
    }

    maybeEmitUpdate();
}

void SolarDataFetcher::maybeEmitUpdate()
{
    if (!m_scalesDone || !m_fluxDone || !m_kpDone) return;

    m_data.sfi       = m_pendingSfi;
    m_data.ssn       = -1;              // not available from current NOAA JSON API
    m_data.aIndex    = m_pendingAIndex;
    m_data.kIndex    = m_pendingKIndex;
    m_data.xrayClass = QString();
    m_data.gScale    = m_pendingG;
    m_data.sScale    = m_pendingS;
    m_data.rScale    = m_pendingR;
    m_data.lastUpdate = QDateTime::currentDateTimeUtc();
    m_data.valid      = (m_data.sfi > 0 || m_data.kIndex >= 0);

    emit updated(m_data);
}
