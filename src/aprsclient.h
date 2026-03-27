#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// APRS-IS TCP client.
// Connects to an APRS-IS server (default: rotate.aprs.net:14580),
// authenticates with callsign + passcode, receives APRS packets.
//
// Usage:
//   auto *aprs = new AprsClient(this);
//   aprs->connectToServer("rotate.aprs.net", 14580, "W5XYZ", "r/JS8Call/50");
//   connect(aprs, &AprsClient::packetReceived, ...);
//   aprs->sendPacket("W5XYZ>APRS,TCPIP*:!3800.00N/09700.00W-JS8Call");

#include <QObject>
#include <QString>
#include <QAbstractSocket>

class QTcpSocket;
class QTimer;

class AprsClient : public QObject {
    Q_OBJECT
public:
    explicit AprsClient(QObject *parent = nullptr);
    ~AprsClient();

    void connectToServer(const QString &host    = QStringLiteral("rotate.aprs.net"),
                         quint16        port    = 14580,
                         const QString &callsign = QString(),
                         const QString &filter  = QString());
    void disconnectFromServer();
    bool isConnected() const;

    // Compute the APRS-IS passcode for a given callsign (base call only, no SSID).
    static int computePasscode(const QString &callsign);

    // Send a raw APRS packet string (newline appended automatically).
    void sendPacket(const QString &packet);

signals:
    void connected();
    void disconnected();
    void packetReceived(const QString &packet);
    void error(const QString &msg);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError err);
    void onReconnectTimer();

private:
    void doLogin();

    QTcpSocket *m_socket;
    QTimer     *m_reconnectTimer;
    QByteArray  m_readBuf;

    QString m_host;
    quint16 m_port = 14580;
    QString m_callsign;
    QString m_filter;
    bool    m_intentionalDisconnect = false;
};
