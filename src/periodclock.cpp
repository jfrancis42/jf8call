// SPDX-License-Identifier: GPL-3.0-or-later
#include "periodclock.h"
#include <QDateTime>

PeriodClock::PeriodClock(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &PeriodClock::onTimer);
}

void PeriodClock::setPeriodSeconds(int secs)
{
    m_periodSeconds = secs;
    if (m_running)
        scheduleNext();
}

void PeriodClock::start()
{
    m_running = true;
    scheduleNext();
}

void PeriodClock::stop()
{
    m_running = false;
    m_timer.stop();
}

void PeriodClock::onTimer()
{
    emit periodStarted(currentUtc());
    if (m_running)
        scheduleNext();
}

void PeriodClock::scheduleNext()
{
    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    const int periodMs = m_periodSeconds * 1000;
    // Next boundary strictly after now
    const qint64 nextBoundaryMs = ((nowMs / periodMs) + 1) * periodMs;
    const int msUntilNext = static_cast<int>(nextBoundaryMs - nowMs);
    m_timer.start(msUntilNext > 0 ? msUntilNext : 1);
}

int PeriodClock::currentUtc()
{
    // JS8Call uses a "code_time" = HHMM (hours*100 + minutes) as UTC int
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    return utc.time().hour() * 100 + utc.time().minute();
}
