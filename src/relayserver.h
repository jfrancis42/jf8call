#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// TCP relay server for store-and-forward message deposit.
// Listens on port 2442 (configurable). Other stations / applications can
// connect and deposit messages using a simple JSON protocol:
//
//   Client → Server: {"cmd":"store","from":"W5XYZ","to":"K7ABC","body":"Hello"}
//   Server → Client: {"ok":true,"id":42}
//   Client → Server: {"cmd":"list","for":"K7ABC"}
//   Server → Client: {"ok":true,"messages":[...]}
//   Client → Server: {"cmd":"delivered","id":42}
//   Server → Client: {"ok":true}

#include <QObject>
#include <QList>
#include <QHash>
#include <QByteArray>

class QTcpServer;
class QTcpSocket;
struct InboxMessage;

class RelayServer : public QObject {
    Q_OBJECT
public:
    explicit RelayServer(QObject *parent = nullptr);
    ~RelayServer();

    bool listen(quint16 port = 2442, bool localhostOnly = true);
    void stopListening();
    bool isListening() const;
    quint16 port() const;

signals:
    void messageDeposited(const InboxMessage &msg);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void handleCommand(QTcpSocket *sock, const QByteArray &line);

    QTcpServer          *m_server;
    QList<QTcpSocket *>  m_clients;
    QHash<QTcpSocket *, QByteArray> m_readBuf;
};
