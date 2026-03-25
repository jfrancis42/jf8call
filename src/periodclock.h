#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// UTC-aligned period boundary clock for JS8 TX/RX synchronization.

#include <QObject>
#include <QTimer>
#include <gfsk8modem.h>

class PeriodClock : public QObject {
    Q_OBJECT
public:
    explicit PeriodClock(QObject *parent = nullptr);

    // Set the active submode. Causes the next period boundary to be
    // recalculated from the current time.
    void setSubmode(gfsk8::Submode submode);
    gfsk8::Submode submode() const { return m_submode; }

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
    gfsk8::Submode m_submode = gfsk8::Submode::Normal;
    bool         m_running = false;
};
