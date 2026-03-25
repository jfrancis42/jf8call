// SPDX-License-Identifier: GPL-3.0-or-later
#include "updatechecker.h"
#include "buildinfo.h"
#include <QNetworkRequest>
#include <QUrl>

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

void UpdateChecker::checkForUpdates()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_reply = m_nam->get(QNetworkRequest(QUrl(QLatin1String(k_buildTimeUrl))));
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onReply);
}

void UpdateChecker::onReply()
{
    if (!m_reply) return;
    const QByteArray body = m_reply->readAll().trimmed();
    m_reply->deleteLater();
    m_reply = nullptr;
    if (body.isEmpty()) return;

    bool ok = false;
    const long long serverTime = body.toLongLong(&ok);
    if (!ok || serverTime <= 0) return;     // unparseable — ignore silently
    if (serverTime <= g_buildTime) return;  // this build is current or newer

    emit updateAvailable();
}
