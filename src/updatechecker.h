#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// UpdateChecker fetches the plain-text build timestamp served at
// k_buildTimeUrl and emits updateAvailable() when the server reports
// a newer build than the one currently running.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    // Perform an async update check.  Safe to call repeatedly; any
    // in-flight request is aborted and replaced.
    void checkForUpdates();

signals:
    void updateAvailable();

private slots:
    void onReply();

private:
    QNetworkAccessManager *m_nam;
    QNetworkReply         *m_reply = nullptr;

    static constexpr const char *k_buildTimeUrl =
        "https://ordo-artificum.com/jf8call-build-time";
};
