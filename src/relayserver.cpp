// SPDX-License-Identifier: GPL-3.0-or-later
#include "relayserver.h"
#include "messageinbox.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>

RelayServer::RelayServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &RelayServer::onNewConnection);
}

RelayServer::~RelayServer()
{
    stopListening();
}

bool RelayServer::listen(quint16 port, bool localhostOnly)
{
    const QHostAddress addr = localhostOnly
        ? QHostAddress::LocalHost
        : QHostAddress::AnyIPv4;
    if (!m_server->listen(addr, port)) {
        qWarning() << "RelayServer: listen failed:" << m_server->errorString();
        return false;
    }
    qDebug() << "RelayServer: listening on"
             << addr.toString() << "port" << m_server->serverPort();
    return true;
}

void RelayServer::stopListening()
{
    for (QTcpSocket *s : m_clients) { s->close(); s->deleteLater(); }
    m_clients.clear();
    m_readBuf.clear();
    m_server->close();
}

bool RelayServer::isListening() const { return m_server->isListening(); }
quint16 RelayServer::port() const { return m_server->serverPort(); }

void RelayServer::onNewConnection()
{
    QTcpSocket *sock = m_server->nextPendingConnection();
    if (!sock) return;
    m_clients.append(sock);
    m_readBuf[sock] = QByteArray();
    connect(sock, &QTcpSocket::readyRead,   this, &RelayServer::onReadyRead);
    connect(sock, &QTcpSocket::disconnected,this, &RelayServer::onDisconnected);
}

void RelayServer::onDisconnected()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    m_clients.removeAll(sock);
    m_readBuf.remove(sock);
    sock->deleteLater();
}

void RelayServer::onReadyRead()
{
    QTcpSocket *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    m_readBuf[sock] += sock->readAll();
    // Process complete newline-delimited JSON lines
    while (true) {
        const int nl = m_readBuf[sock].indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = m_readBuf[sock].left(nl).trimmed();
        m_readBuf[sock] = m_readBuf[sock].mid(nl + 1);
        if (!line.isEmpty())
            handleCommand(sock, line);
    }
}

static void sendReply(QTcpSocket *sock, const QJsonObject &obj)
{
    sock->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n");
}

void RelayServer::handleCommand(QTcpSocket *sock, const QByteArray &line)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject err;
        err[QStringLiteral("ok")]    = false;
        err[QStringLiteral("error")] = QStringLiteral("invalid JSON");
        sendReply(sock, err);
        return;
    }

    const QJsonObject req = doc.object();
    const QString cmd = req.value(QStringLiteral("cmd")).toString();

    if (cmd == QStringLiteral("store")) {
        const QString from = req.value(QStringLiteral("from")).toString().toUpper().trimmed();
        const QString to   = req.value(QStringLiteral("to")).toString().toUpper().trimmed();
        const QString body = req.value(QStringLiteral("body")).toString().trimmed();
        if (from.isEmpty() || to.isEmpty() || body.isEmpty()) {
            QJsonObject err;
            err[QStringLiteral("ok")]    = false;
            err[QStringLiteral("error")] = QStringLiteral("from, to, body required");
            sendReply(sock, err);
            return;
        }
        InboxMessage im;
        im.utc  = QDateTime::currentDateTimeUtc();
        im.from = from;
        im.to   = to;
        im.text = body;
        const int id = MessageInbox::instance().store(im);
        emit messageDeposited(im);
        QJsonObject rep;
        rep[QStringLiteral("ok")] = true;
        rep[QStringLiteral("id")] = id;
        sendReply(sock, rep);

    } else if (cmd == QStringLiteral("list")) {
        const QString forCall = req.value(QStringLiteral("for")).toString().toUpper().trimmed();
        const QList<InboxMessage> msgs = forCall.isEmpty()
            ? MessageInbox::instance().messages()
            : MessageInbox::instance().relayMessagesFor(forCall);
        QJsonArray arr;
        for (const InboxMessage &m : msgs) {
            QJsonObject o;
            o[QStringLiteral("id")]        = m.id;
            o[QStringLiteral("utc")]       = m.utc.toString(Qt::ISODate);
            o[QStringLiteral("from")]      = m.from;
            o[QStringLiteral("to")]        = m.to;
            o[QStringLiteral("body")]      = m.text;
            o[QStringLiteral("delivered")] = m.delivered;
            arr.append(o);
        }
        QJsonObject rep;
        rep[QStringLiteral("ok")]       = true;
        rep[QStringLiteral("messages")] = arr;
        sendReply(sock, rep);

    } else if (cmd == QStringLiteral("delivered")) {
        const int id = req.value(QStringLiteral("id")).toInt(-1);
        if (id > 0) MessageInbox::instance().markDelivered(id);
        QJsonObject rep;
        rep[QStringLiteral("ok")] = true;
        sendReply(sock, rep);

    } else if (cmd == QStringLiteral("ping")) {
        QJsonObject rep;
        rep[QStringLiteral("ok")]   = true;
        rep[QStringLiteral("pong")] = QStringLiteral("JF8Call relay");
        sendReply(sock, rep);

    } else {
        QJsonObject err;
        err[QStringLiteral("ok")]    = false;
        err[QStringLiteral("error")] = QStringLiteral("unknown command: ") + cmd;
        sendReply(sock, err);
    }
}
