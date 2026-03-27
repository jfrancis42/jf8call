// SPDX-License-Identifier: GPL-3.0-or-later
#include "aprsclient.h"

#include <QTcpSocket>
#include <QTimer>
#include <QAbstractSocket>
#include <QDebug>
#include <cctype>

AprsClient::AprsClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setInterval(60 * 1000);  // retry every 60 s
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected,
            this, &AprsClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &AprsClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &AprsClient::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &AprsClient::onError);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &AprsClient::onReconnectTimer);
}

AprsClient::~AprsClient()
{
    m_intentionalDisconnect = true;
    m_socket->abort();
}

// APRS-IS passcode: XOR hash of base callsign (no SSID)
int AprsClient::computePasscode(const QString &callsign)
{
    // Strip SSID
    const QString base = callsign.toUpper().section(QLatin1Char('-'), 0, 0);
    int hash = 0x73e2;
    const QByteArray ba = base.toLatin1();
    const char *p = ba.constData();
    while (*p) {
        hash ^= toupper(static_cast<unsigned char>(*p++)) << 8;
        if (*p) hash ^= toupper(static_cast<unsigned char>(*p++));
    }
    return hash & 0x7fff;
}

void AprsClient::connectToServer(const QString &host, quint16 port,
                                  const QString &callsign, const QString &filter)
{
    m_host     = host;
    m_port     = port;
    m_callsign = callsign.isEmpty() ? callsign : callsign.toUpper();
    m_filter   = filter;
    m_intentionalDisconnect = false;
    m_readBuf.clear();

    m_socket->connectToHost(host, port);
}

void AprsClient::disconnectFromServer()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer->stop();
    m_socket->disconnectFromHost();
}

bool AprsClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void AprsClient::sendPacket(const QString &packet)
{
    if (!isConnected()) return;
    const QString line = packet.endsWith(QLatin1Char('\n')) ? packet : packet + QLatin1Char('\n');
    m_socket->write(line.toUtf8());
}

void AprsClient::onConnected()
{
    doLogin();
    emit connected();
}

void AprsClient::doLogin()
{
    // APRS-IS login line:
    // user CALLSIGN pass PASSCODE vers JF8Call x.y.z [filter FILTER]
    const int passcode = m_callsign.isEmpty() ? -1 : computePasscode(m_callsign);
    QString login = QStringLiteral("user %1 pass %2 vers JF8Call 0.5.2")
        .arg(m_callsign.isEmpty() ? QStringLiteral("NOCALL") : m_callsign)
        .arg(passcode);
    if (!m_filter.isEmpty())
        login += QStringLiteral(" filter %1").arg(m_filter);
    login += QLatin1Char('\n');
    m_socket->write(login.toUtf8());
}

void AprsClient::onDisconnected()
{
    emit disconnected();
    if (!m_intentionalDisconnect) {
        qDebug() << "AprsClient: disconnected, will retry in 60s";
        m_reconnectTimer->start();
    }
}

void AprsClient::onReadyRead()
{
    m_readBuf += m_socket->readAll();
    while (true) {
        const int nl = m_readBuf.indexOf('\n');
        if (nl < 0) break;
        const QByteArray lineBytes = m_readBuf.left(nl).trimmed();
        m_readBuf = m_readBuf.mid(nl + 1);
        if (lineBytes.isEmpty()) continue;
        const QString line = QString::fromUtf8(lineBytes);
        // Skip comments (lines starting with '#') — these are server status lines
        if (line.startsWith(QLatin1Char('#'))) continue;
        emit packetReceived(line);
    }
}

void AprsClient::onError(QAbstractSocket::SocketError)
{
    emit error(m_socket->errorString());
    if (!m_intentionalDisconnect)
        m_reconnectTimer->start();
}

void AprsClient::onReconnectTimer()
{
    if (!m_intentionalDisconnect && !isConnected()) {
        qDebug() << "AprsClient: reconnecting to" << m_host << "port" << m_port;
        m_readBuf.clear();
        m_socket->connectToHost(m_host, m_port);
    }
}
