// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>

// ── BandEntry serialisation ──────────────────────────────────────────────────

QJsonObject BandEntry::toJson() const
{
    QJsonObject o;
    o[u"name"]     = name;
    o[u"freqKhz"]  = freqKhz;
    o[u"txFreqHz"] = txFreqHz;
    return o;
}

BandEntry BandEntry::fromJson(const QJsonObject &o)
{
    BandEntry e;
    e.name     = o.value(u"name").toString();
    e.freqKhz  = o.value(u"freqKhz").toDouble(14078.0);
    e.txFreqHz = o.value(u"txFreqHz").toDouble(1500.0);
    return e;
}

QList<BandEntry> defaultBandList()
{
    return {
        { QStringLiteral("160m"),  1840.0, 1500.0 },
        { QStringLiteral("80m"),   3578.0, 1500.0 },
        { QStringLiteral("60m"),   5357.0, 1500.0 },
        { QStringLiteral("40m"),   7078.0, 1500.0 },
        { QStringLiteral("30m"), 10130.0,  1500.0 },
        { QStringLiteral("20m"), 14078.0,  1500.0 },
        { QStringLiteral("17m"), 18104.0,  1500.0 },
        { QStringLiteral("15m"), 21078.0,  1500.0 },
        { QStringLiteral("12m"), 24922.0,  1500.0 },
        { QStringLiteral("10m"), 28078.0,  1500.0 },
        { QStringLiteral("6m"),  50318.0,  1500.0 },
    };
}

// ── FreqScheduleEntry serialisation ─────────────────────────────────────────

QJsonObject FreqScheduleEntry::toJson() const
{
    QJsonObject o;
    o[u"enabled"]   = enabled;
    o[u"utcHhmm"]   = utcHhmm;
    o[u"dayMask"]   = static_cast<int>(dayMask);
    o[u"freqKhz"]   = freqKhz;
    o[u"txFreqHz"]  = txFreqHz;
    o[u"label"]     = label;
    return o;
}

FreqScheduleEntry FreqScheduleEntry::fromJson(const QJsonObject &o)
{
    FreqScheduleEntry e;
    e.enabled  = o.value(u"enabled").toBool(true);
    e.utcHhmm  = o.value(u"utcHhmm").toInt(0);
    e.dayMask  = static_cast<quint8>(o.value(u"dayMask").toInt(0x7F));
    e.freqKhz  = o.value(u"freqKhz").toDouble(14078.0);
    e.txFreqHz = o.value(u"txFreqHz").toDouble(1500.0);
    e.label    = o.value(u"label").toString();
    return e;
}

static QString configDir()
{
    return QDir::homePath() + QStringLiteral("/.jf8call");
}

void Config::ensureDir()
{
    const QString dir = configDir();
    QFileInfo fi(dir);
    if (fi.exists() && !fi.isDir())
        QFile::remove(dir);
    QDir().mkpath(dir);
}

QString Config::filePath()
{
    return configDir() + QStringLiteral("/settings.json");
}

Config Config::load()
{
    Config c;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return c;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return c;

    const QJsonObject o = doc.object();
    c.callsign                  = o.value(u"callsign").toString();
    c.grid                      = o.value(u"grid").toString();
    c.audioInputName            = o.value(u"audioInputName").toString();
    c.audioOutputName           = o.value(u"audioOutputName").toString();
    c.modemType                 = o.value(u"modemType").toInt(0);
    c.submode                   = o.value(u"submode").toInt(0);
    c.frequencyKhz              = o.value(u"frequencyKhz").toDouble(14078.0);
    c.txFreqHz                  = o.value(u"txFreqHz").toDouble(1500.0);
    c.txPowerPct                = o.value(u"txPowerPct").toInt(50);
    c.stationInfo               = o.value(u"stationInfo").toString();
    c.stationStatus             = o.value(u"stationStatus").toString();
    c.cqMessage                 = o.value(u"cqMessage").toString();
    c.heartbeatEnabled          = o.value(u"heartbeatEnabled").toBool(false);
    c.heartbeatIntervalMins     = o.value(u"heartbeatIntervalMins").toInt(10);
    c.heartbeatSubChannel       = o.value(u"heartbeatSubChannel").toBool(true);
    c.txEnabled                 = o.value(u"txEnabled").toBool(true);
    c.autoReply                 = o.value(u"autoReply").toBool(true);
    c.distMiles                 = o.value(u"distMiles").toBool(true);
    c.autoAtu                   = o.value(u"autoAtu").toBool(false);
    c.rigModel      = o.value(u"rigModel").toInt(1);
    c.rigPort       = o.value(u"rigPort").toString();
    c.rigBaud       = o.value(u"rigBaud").toInt(9600);
    c.rigDataBits   = o.value(u"rigDataBits").toInt(8);
    c.rigStopBits   = o.value(u"rigStopBits").toInt(1);
    c.rigParity     = o.value(u"rigParity").toInt(0);
    c.rigHandshake  = o.value(u"rigHandshake").toInt(0);
    c.rigDtrState   = o.value(u"rigDtrState").toInt(0);
    c.rigRtsState   = o.value(u"rigRtsState").toInt(0);
    c.pttType       = o.value(u"pttType").toInt(0);
    c.pskReporterEnabled        = o.value(u"pskReporterEnabled").toBool(true);
    c.wsEnabled                 = o.value(u"wsEnabled").toBool(true);
    c.wsPort                    = o.value(u"wsPort").toInt(2102);
    c.wsHost                    = o.value(u"wsHost").toString(QStringLiteral("127.0.0.1"));
    c.emulatedSplit             = o.value(u"emulatedSplit").toBool(false);
    c.relayServerEnabled        = o.value(u"relayServerEnabled").toBool(false);
    c.relayServerPort           = o.value(u"relayServerPort").toInt(2442);
    c.relayServerLocalhostOnly  = o.value(u"relayServerLocalhostOnly").toBool(true);
    c.qsoLogEnabled             = o.value(u"qsoLogEnabled").toBool(true);
    c.aprsEnabled               = o.value(u"aprsEnabled").toBool(false);
    c.aprsHost                  = o.value(u"aprsHost").toString(QStringLiteral("rotate.aprs.net"));
    c.aprsPort                  = o.value(u"aprsPort").toInt(14580);
    c.aprsFilter                = o.value(u"aprsFilter").toString();
    c.solarEnabled              = o.value(u"solarEnabled").toBool(true);
    c.infoMaxAgeMins            = o.value(u"infoMaxAgeMins").toInt(30);
    c.heardMaxAgeMins           = o.value(u"heardMaxAgeMins").toInt(30);
    c.waterfallGain             = static_cast<float>(o.value(u"waterfallGain").toDouble(0.0));
    c.waterfallMode             = o.value(u"waterfallMode").toInt(0);
    for (const QJsonValue &v : o.value(u"bandList").toArray())
        c.bandList.append(BandEntry::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value(u"freqSchedule").toArray())
        c.freqSchedule.append(FreqScheduleEntry::fromJson(v.toObject()));
    if (o.contains(u"groups")) {
        for (const QJsonValue &v : o.value(u"groups").toArray())
            c.groups.append(v.toString().toUpper());
    }
    // else: default {QStringLiteral("@ALL")} from struct initialiser
    c.windowGeometry = QByteArray::fromBase64(
        o.value(u"windowGeometry").toString().toLatin1());
    c.windowState = QByteArray::fromBase64(
        o.value(u"windowState").toString().toLatin1());
    c.vSplitterState = QByteArray::fromBase64(
        o.value(u"vSplitterState").toString().toLatin1());
    c.hSplitterState = QByteArray::fromBase64(
        o.value(u"hSplitterState").toString().toLatin1());
    return c;
}

void Config::save() const
{
    ensureDir();
    QJsonObject o;
    o[u"callsign"]                 = callsign;
    o[u"grid"]                     = grid;
    o[u"audioInputName"]           = audioInputName;
    o[u"audioOutputName"]          = audioOutputName;
    o[u"modemType"]                = modemType;
    o[u"submode"]                  = submode;
    o[u"frequencyKhz"]             = frequencyKhz;
    o[u"txFreqHz"]                 = txFreqHz;
    o[u"txPowerPct"]               = txPowerPct;
    o[u"stationInfo"]              = stationInfo;
    o[u"stationStatus"]            = stationStatus;
    o[u"cqMessage"]                = cqMessage;
    o[u"heartbeatEnabled"]         = heartbeatEnabled;
    o[u"heartbeatIntervalMins"]    = heartbeatIntervalMins;
    o[u"heartbeatSubChannel"]      = heartbeatSubChannel;
    o[u"txEnabled"]                = txEnabled;
    o[u"autoReply"]                = autoReply;
    o[u"distMiles"]                = distMiles;
    o[u"autoAtu"]                  = autoAtu;
    o[u"rigModel"]      = rigModel;
    o[u"rigPort"]       = rigPort;
    o[u"rigBaud"]       = rigBaud;
    o[u"rigDataBits"]   = rigDataBits;
    o[u"rigStopBits"]   = rigStopBits;
    o[u"rigParity"]     = rigParity;
    o[u"rigHandshake"]  = rigHandshake;
    o[u"rigDtrState"]   = rigDtrState;
    o[u"rigRtsState"]   = rigRtsState;
    o[u"pttType"]       = pttType;
    o[u"pskReporterEnabled"]       = pskReporterEnabled;
    o[u"wsEnabled"]                = wsEnabled;
    o[u"wsPort"]                   = wsPort;
    o[u"wsHost"]                   = wsHost;
    o[u"emulatedSplit"]            = emulatedSplit;
    o[u"relayServerEnabled"]       = relayServerEnabled;
    o[u"relayServerPort"]          = relayServerPort;
    o[u"relayServerLocalhostOnly"] = relayServerLocalhostOnly;
    o[u"qsoLogEnabled"]            = qsoLogEnabled;
    o[u"aprsEnabled"]              = aprsEnabled;
    o[u"aprsHost"]                 = aprsHost;
    o[u"aprsPort"]                 = aprsPort;
    o[u"aprsFilter"]               = aprsFilter;
    o[u"solarEnabled"]             = solarEnabled;
    o[u"infoMaxAgeMins"]           = infoMaxAgeMins;
    o[u"heardMaxAgeMins"]          = heardMaxAgeMins;
    o[u"waterfallGain"]            = static_cast<double>(waterfallGain);
    o[u"waterfallMode"]            = waterfallMode;
    {
        QJsonArray arr;
        for (const BandEntry &e : bandList)
            arr.append(e.toJson());
        o[u"bandList"] = arr;
    }
    {
        QJsonArray arr;
        for (const FreqScheduleEntry &e : freqSchedule)
            arr.append(e.toJson());
        o[u"freqSchedule"] = arr;
    }
    {
        QJsonArray arr;
        for (const QString &g : groups)
            arr.append(g);
        o[u"groups"] = arr;
    }
    o[u"windowGeometry"]           = QString::fromLatin1(windowGeometry.toBase64());
    o[u"windowState"]              = QString::fromLatin1(windowState.toBase64());
    o[u"vSplitterState"]           = QString::fromLatin1(vSplitterState.toBase64());
    o[u"hSplitterState"]           = QString::fromLatin1(hSplitterState.toBase64());

    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson());
}
