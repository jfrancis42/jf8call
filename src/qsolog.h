#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// QSO log with SQLite backend and ADIF import/export.
// Singleton: QsoLog::instance().

#include <QString>
#include <QDateTime>
#include <QList>

struct QsoEntry {
    int       id        = 0;
    QDateTime utc;
    QString   callsign;          // contacted station
    QString   grid;              // their grid square (empty if unknown)
    QString   band;              // "20m", "40m", etc.
    QString   mode;              // "JS8", "Olivia", "PSK63", etc.
    double    freqKhz   = 0.0;  // dial frequency
    double    txFreqHz  = 0.0;  // audio TX offset
    int       snrDb     = 0;    // received SNR
    QString   sentRst   = QStringLiteral("599");
    QString   rcvdRst   = QStringLiteral("599");
    QString   notes;
};

class QSqlDatabase;

class QsoLog {
public:
    static QsoLog &instance();

    // Add a QSO. Returns the assigned id (> 0), or -1 on error.
    int  addQso(const QsoEntry &e);

    // Remove a QSO by id.
    void removeQso(int id);

    // Remove all QSOs.
    void clear();

    // Retrieve all QSOs, newest first.
    QList<QsoEntry> all() const;

    // Count of logged QSOs.
    int count() const;

    // Export as ADIF 3 string (suitable for writing to a .adi file).
    QString exportAdif() const;

    // Import ADIF text, appending new QSOs. Returns number added.
    // On error, sets *errorMsg and returns -1.
    int importAdif(const QString &adif, QString *errorMsg = nullptr);

    bool isOpen() const { return m_open; }

private:
    QsoLog();
    bool openDb();

    bool m_open = false;
    static QString dbPath();
};
