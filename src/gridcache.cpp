// SPDX-License-Identifier: GPL-3.0-or-later
#include "gridcache.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

static QString configDir()
{
    return QDir::homePath() + QStringLiteral("/.jf8call");
}

QString GridCache::filePath()
{
    return configDir() + QStringLiteral("/grids.json");
}

GridCache &GridCache::instance()
{
    static GridCache s;
    if (!s.m_loaded) s.load();
    return s;
}

GridCache::GridCache() = default;

void GridCache::load()
{
    m_loaded = true;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const QJsonObject o = doc.object();
    for (auto it = o.begin(); it != o.end(); ++it)
        m_cache[it.key().toUpper()] = it.value().toString().toUpper();
}

void GridCache::save() const
{
    QDir().mkpath(configDir());
    QJsonObject o;
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it)
        o[it.key()] = it.value();
    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson());
}

QString GridCache::get(const QString &callsign) const
{
    return m_cache.value(callsign.toUpper());
}

void GridCache::set(const QString &callsign, const QString &grid)
{
    if (callsign.isEmpty() || grid.isEmpty()) return;
    const QString call = callsign.toUpper();
    const QString gr   = grid.toUpper();
    if (m_cache.value(call) == gr) return;  // no change, skip save
    m_cache[call] = gr;
    save();
}
