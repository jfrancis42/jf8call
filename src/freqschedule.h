#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Automatic frequency schedule entry.
// The scheduler checks every minute; if the current UTC time matches an
// enabled entry, it commands a frequency change.

#include <QString>
#include <QJsonObject>

struct FreqScheduleEntry {
    bool    enabled  = true;
    int     utcHhmm  = 0;       // UTC time to activate: 0000–2359 (e.g. 1430 = 14:30)
    quint8  dayMask  = 0x7F;    // bitmask: bit0=Mon,bit1=Tue,...,bit6=Sun; 0x7F=every day
    double  freqKhz  = 14078.0; // dial frequency to switch to (kHz)
    double  txFreqHz = 1500.0;  // audio TX offset (Hz)
    QString label;              // optional human-readable description

    // Returns true if entry should fire at the given UTC date/time
    bool matchesTime(int utcHHMM, int dayOfWeekQt) const;
    // Qt day-of-week: 1=Mon … 7=Sun → bit0..bit6

    QJsonObject toJson() const;
    static FreqScheduleEntry fromJson(const QJsonObject &o);
};

inline bool FreqScheduleEntry::matchesTime(int utcHHMM, int dayOfWeekQt) const
{
    if (!enabled) return false;
    if (utcHHMM != utcHhmm) return false;
    // Qt day: 1=Mon..7=Sun → bitmask bit (day-1)
    const int bit = (dayOfWeekQt >= 1 && dayOfWeekQt <= 7) ? (dayOfWeekQt - 1) : -1;
    if (bit < 0) return false;
    return (dayMask & (1u << bit)) != 0;
}

// Standard JS8 calling frequencies by band
struct BandPreset {
    QString name;         // "20m", "40m", etc.
    double  freqKhz;      // JS8 calling dial frequency
    double  txFreqHz;     // default TX offset
};

inline QList<BandPreset> standardBandPresets()
{
    // clang-format off
    return {
        { QStringLiteral("160m"),  1.842e3,   1500.0 },
        { QStringLiteral("80m"),   3.578e3,   1500.0 },
        { QStringLiteral("60m"),   5.359e3,   1500.0 },
        { QStringLiteral("40m"),   7.078e3,   1500.0 },
        { QStringLiteral("30m"),  10.130e3,   1500.0 },
        { QStringLiteral("20m"),  14.078e3,   1500.0 },
        { QStringLiteral("17m"),  18.104e3,   1500.0 },
        { QStringLiteral("15m"),  21.078e3,   1500.0 },
        { QStringLiteral("12m"),  24.922e3,   1500.0 },
        { QStringLiteral("10m"),  28.078e3,   1500.0 },
        { QStringLiteral("6m"),   50.318e3,   1500.0 },
        { QStringLiteral("2m"),  144.178e3,   1500.0 },
    };
    // clang-format on
}

// Returns band name (e.g. "20m") for a given frequency in kHz, or empty string.
inline QString bandForFreqKhz(double khz)
{
    if (khz >=  1800 && khz <  2000)  return QStringLiteral("160m");
    if (khz >=  3500 && khz <  4000)  return QStringLiteral("80m");
    if (khz >=  5250 && khz <  5450)  return QStringLiteral("60m");
    if (khz >=  7000 && khz <  7300)  return QStringLiteral("40m");
    if (khz >= 10100 && khz < 10150)  return QStringLiteral("30m");
    if (khz >= 14000 && khz < 14350)  return QStringLiteral("20m");
    if (khz >= 18068 && khz < 18168)  return QStringLiteral("17m");
    if (khz >= 21000 && khz < 21450)  return QStringLiteral("15m");
    if (khz >= 24890 && khz < 24990)  return QStringLiteral("12m");
    if (khz >= 28000 && khz < 29700)  return QStringLiteral("10m");
    if (khz >= 50000 && khz < 54000)  return QStringLiteral("6m");
    if (khz >=144000 && khz <148000)  return QStringLiteral("2m");
    return QString();
}
