// SPDX-License-Identifier: GPL-3.0-or-later
#include "messageinbox.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static QString configDir() { return QDir::homePath() + QStringLiteral("/.jf8call"); }

QString MessageInbox::filePath()
{
    return configDir() + QStringLiteral("/inbox.json");
}

MessageInbox &MessageInbox::instance()
{
    static MessageInbox s;
    if (!s.m_loaded) s.load();
    return s;
}

MessageInbox::MessageInbox() = default;

void MessageInbox::load()
{
    m_loaded = true;
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();
    m_nextId = root.value(u"next_id").toInt(1);
    for (const QJsonValue &v : root.value(u"messages").toArray()) {
        const QJsonObject o = v.toObject();
        InboxMessage m;
        m.id        = o.value(u"id").toInt();
        m.utc       = QDateTime::fromString(o.value(u"utc").toString(), Qt::ISODate);
        m.from      = o.value(u"from").toString();
        m.to        = o.value(u"to").toString();
        m.text      = o.value(u"text").toString();
        m.relayVia  = o.value(u"relay_via").toString();
        m.freqHz    = o.value(u"freq_hz").toDouble();
        m.snrDb     = o.value(u"snr_db").toInt();
        m.read      = o.value(u"read").toBool(false);
        m.delivered = o.value(u"delivered").toBool(false);
        m_messages.append(m);
    }
}

void MessageInbox::save() const
{
    QDir().mkpath(configDir());
    QJsonArray arr;
    for (const InboxMessage &m : m_messages) {
        QJsonObject o;
        o[u"id"]        = m.id;
        o[u"utc"]       = m.utc.toString(Qt::ISODate);
        o[u"from"]      = m.from;
        o[u"to"]        = m.to;
        o[u"text"]      = m.text;
        o[u"relay_via"] = m.relayVia;
        o[u"freq_hz"]   = m.freqHz;
        o[u"snr_db"]    = m.snrDb;
        o[u"read"]      = m.read;
        o[u"delivered"] = m.delivered;
        arr.append(o);
    }
    QJsonObject root;
    root[u"next_id"]  = m_nextId;
    root[u"messages"] = arr;
    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson());
}

int MessageInbox::store(const InboxMessage &msg)
{
    InboxMessage m = msg;
    m.id = m_nextId++;
    if (!m.utc.isValid()) m.utc = QDateTime::currentDateTimeUtc();
    m_messages.prepend(m);     // newest first
    save();
    return m.id;
}

const QList<InboxMessage> &MessageInbox::messages() const
{
    return m_messages;
}

QList<InboxMessage> MessageInbox::messagesForMe(const QString &mycall) const
{
    const QString mc = mycall.toUpper();
    QList<InboxMessage> out;
    for (const InboxMessage &m : m_messages) {
        const QString t = m.to.toUpper();
        if (t == mc || t.startsWith(QLatin1Char('@')))
            out.append(m);
    }
    return out;
}

QList<InboxMessage> MessageInbox::relayMessagesFor(const QString &callsign) const
{
    const QString c = callsign.toUpper();
    QList<InboxMessage> out;
    for (const InboxMessage &m : m_messages) {
        if (m.to.toUpper() == c && !m.delivered)
            out.append(m);
    }
    return out;
}

int MessageInbox::unreadCount(const QString &mycall) const
{
    const QString mc = mycall.toUpper();
    int n = 0;
    for (const InboxMessage &m : m_messages) {
        const QString t = m.to.toUpper();
        if ((t == mc || t.startsWith(QLatin1Char('@'))) && !m.read)
            ++n;
    }
    return n;
}

void MessageInbox::markRead(int id)
{
    for (InboxMessage &m : m_messages) {
        if (m.id == id && !m.read) { m.read = true; save(); return; }
    }
}

void MessageInbox::markDelivered(int id)
{
    for (InboxMessage &m : m_messages) {
        if (m.id == id && !m.delivered) { m.delivered = true; save(); return; }
    }
}

void MessageInbox::remove(int id)
{
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].id == id) { m_messages.removeAt(i); save(); return; }
    }
}

void MessageInbox::clear()
{
    m_messages.clear();
    save();
}
