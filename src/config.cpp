// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

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
    c.submode                   = o.value(u"submode").toInt(0);
    c.frequencyKhz              = o.value(u"frequencyKhz").toDouble(14078.0);
    c.txFreqHz                  = o.value(u"txFreqHz").toDouble(1500.0);
    c.txPowerPct                = o.value(u"txPowerPct").toInt(50);
    c.stationInfo               = o.value(u"stationInfo").toString();
    c.stationStatus             = o.value(u"stationStatus").toString();
    c.cqMessage                 = o.value(u"cqMessage").toString();
    c.heartbeatEnabled          = o.value(u"heartbeatEnabled").toBool(true);
    c.heartbeatIntervalPeriods  = o.value(u"heartbeatIntervalPeriods").toInt(4);
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
    c.wsEnabled                 = o.value(u"wsEnabled").toBool(true);
    c.wsPort                    = o.value(u"wsPort").toInt(2102);
    c.wsHost                    = o.value(u"wsHost").toString(QStringLiteral("127.0.0.1"));
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
    o[u"submode"]                  = submode;
    o[u"frequencyKhz"]             = frequencyKhz;
    o[u"txFreqHz"]                 = txFreqHz;
    o[u"txPowerPct"]               = txPowerPct;
    o[u"stationInfo"]              = stationInfo;
    o[u"stationStatus"]            = stationStatus;
    o[u"cqMessage"]                = cqMessage;
    o[u"heartbeatEnabled"]         = heartbeatEnabled;
    o[u"heartbeatIntervalPeriods"] = heartbeatIntervalPeriods;
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
    o[u"wsEnabled"]                = wsEnabled;
    o[u"wsPort"]                   = wsPort;
    o[u"wsHost"]                   = wsHost;
    o[u"windowGeometry"]           = QString::fromLatin1(windowGeometry.toBase64());
    o[u"windowState"]              = QString::fromLatin1(windowState.toBase64());
    o[u"vSplitterState"]           = QString::fromLatin1(vSplitterState.toBase64());
    o[u"hSplitterState"]           = QString::fromLatin1(hSplitterState.toBase64());

    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson());
}
