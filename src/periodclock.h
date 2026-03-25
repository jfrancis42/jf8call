#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// UTC-aligned period boundary clock for JS8 TX/RX synchronization.

#include <QObject>
#include <QTimer>

class PeriodClock : public QObject {
    Q_OBJECT
public:
    explicit PeriodClock(QObject *parent = nullptr);

    // Set the period length in seconds. Causes the next period boundary to be
    // recalculated from the current time.
    void setPeriodSeconds(int secs);
    int periodSeconds() const { return m_periodSeconds; }

    void start();
    void stop();

    // Returns the UTC code_time() value for the current period
    // (seconds since midnight, rounded to nearest period).
    static int currentUtc();

signals:
    // Emitted at each period boundary.
    // utc: code_time() value for this period
    void periodStarted(int utc);

private slots:
    void onTimer();

private:
    void scheduleNext();

    QTimer      m_timer;
    int          m_periodSeconds = 15;  // Normal submode default
    bool         m_running = false;
};
