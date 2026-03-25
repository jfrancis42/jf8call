#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent store for received JS8 MSG messages.
// Stores both direct messages (to our callsign) and relay messages
// (addressed to a third station that we are holding for delivery).
// Persisted to ~/.jf8call/inbox.json.

#include <QString>
#include <QDateTime>
#include <QList>

struct InboxMessage {
    int       id        = 0;
    QDateTime utc;
    QString   from;          // sender callsign
    QString   to;            // destination callsign / @ALL / @group
    QString   text;          // message body (checksum already stripped)
    QString   relayVia;      // non-empty if delivered via a relay station
    double    freqHz    = 0.0;
    int       snrDb     = 0;
    bool      read      = false;   // for messages addressed to us
    bool      delivered = false;   // for relay messages we hold for others
};

class MessageInbox {
public:
    static MessageInbox &instance();

    // Store a message.  Returns the assigned id.
    int  store(const InboxMessage &msg);

    // All stored messages (direct + relay).
    const QList<InboxMessage> &messages() const;

    // Messages addressed directly to mycall or @ALL / @group.
    QList<InboxMessage> messagesForMe(const QString &mycall) const;

    // Undelivered relay messages stored for a specific callsign.
    QList<InboxMessage> relayMessagesFor(const QString &callsign) const;

    // Count of unread messages addressed to mycall.
    int  unreadCount(const QString &mycall) const;

    // Mark message as read (for our own messages).
    void markRead(int id);

    // Mark relay message as delivered; save.
    void markDelivered(int id);

    // Delete a message.
    void remove(int id);

    // Remove all messages.
    void clear();

private:
    MessageInbox();
    void load();
    void save() const;
    static QString filePath();

    QList<InboxMessage> m_messages;
    int  m_nextId = 1;
    bool m_loaded = false;
};
