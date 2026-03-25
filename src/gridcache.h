#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent cache mapping callsign → most-recently-heard Maidenhead grid.
// Saved to ~/.jf8call/grids.json; auto-loaded on first use.

#include <QString>
#include <QHash>

class GridCache {
public:
    // Retrieve the singleton instance (lazy-loads on first call).
    static GridCache &instance();

    // Return the cached grid for callsign, or empty string if unknown.
    QString get(const QString &callsign) const;

    // Update (or insert) a callsign → grid mapping and persist to disk.
    void set(const QString &callsign, const QString &grid);

private:
    GridCache();
    void load();
    void save() const;
    static QString filePath();

    QHash<QString, QString> m_cache;
    bool m_loaded = false;
};
