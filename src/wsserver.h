#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call WebSocket API server.
//
// Single endpoint: ws://localhost:2102  (port configurable)
// All messages are UTF-8 JSON text frames.
//
// Protocol:
//   Client → Server:  {"type":"cmd","id":"<opaque>","cmd":"<name>","data":{...}}
//   Server → Client:  {"type":"reply","id":"<same>","ok":true,"data":{...}}
//                  or {"type":"reply","id":"<same>","ok":false,"error":"..."}
//   Server → Client:  {"type":"event","event":"<name>","data":{...}}
//
// The "id" field in commands is echoed back in the reply so the client can
// correlate async responses. It is optional; omit for fire-and-forget commands.
//
// Events are broadcast to ALL connected clients without a request id.

#include <QObject>
#include <QTimer>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QList>
#include <QJsonObject>
#include <vector>

class MainWindow;
#include "js8message.h"

class WsServer : public QObject {
    Q_OBJECT
public:
    explicit WsServer(MainWindow *app, QObject *parent = nullptr);
    ~WsServer();

    bool listen(quint16 port = 2102, const QString &host = QStringLiteral("127.0.0.1"));
    void stopListening();
    bool isListening() const;
    quint16 port() const;
    int clientCount() const { return m_clients.size(); }

    // ── Push events to all connected clients ─────────────────────────────
    // Called by MainWindow at the appropriate points in the application.

    void pushMessageDecoded(const JS8Message &msg);
    void pushMessageFrame(float freqHz, int snrDb, int submode, int modemType,
                          int frameType, const QString &frameText,
                          const QString &assembledText, const QDateTime &utc);
    void pushSpectrum(const std::vector<float> &bins, float sampleRateHz);
    void pushStatus();
    void pushTxStarted();
    void pushTxFinished();
    void pushRadioConnected(double khz, const QString &mode);
    void pushRadioDisconnected();
    void pushConfigChanged();
    void pushTxQueued(int queueSize);
    void pushError(const QString &msg);

private slots:
    void onNewConnection();
    void onTextMessage(const QString &text);
    void onDisconnected();

private:
    void handleCommand(QWebSocket *client, const QJsonObject &msg);
    void reply(QWebSocket *client, const QString &id, bool ok,
               const QJsonObject &data = {}, const QString &error = {});
    void broadcast(const QString &event, const QJsonObject &data = {});
    void send(QWebSocket *client, const QJsonObject &msg);

    // ── Command handlers ─────────────────────────────────────────────────
    // Each returns the "data" object for the reply (throws QString on error).

    QJsonObject cmdStatusGet(const QJsonObject &d);
    QJsonObject cmdConfigGet(const QJsonObject &d);
    QJsonObject cmdConfigSet(const QJsonObject &d);
    QJsonObject cmdAudioDevices(const QJsonObject &d);
    QJsonObject cmdAudioRestart(const QJsonObject &d);
    QJsonObject cmdRadioGet(const QJsonObject &d);
    QJsonObject cmdRadioConnect(const QJsonObject &d);
    QJsonObject cmdRadioDisconnect(const QJsonObject &d);
    QJsonObject cmdRadioFreqSet(const QJsonObject &d);
    QJsonObject cmdRadioPttSet(const QJsonObject &d);
    QJsonObject cmdRadioTune(const QJsonObject &d);
    QJsonObject cmdRadioPowerGet(const QJsonObject &d);
    QJsonObject cmdRadioPowerSet(const QJsonObject &d);
    QJsonObject cmdRadioVolumeGet(const QJsonObject &d);
    QJsonObject cmdRadioVolumeSet(const QJsonObject &d);
    QJsonObject cmdRadioMute(const QJsonObject &d);
    QJsonObject cmdRadioUnmute(const QJsonObject &d);
    QJsonObject cmdMessagesGet(const QJsonObject &d);
    QJsonObject cmdMessagesClear(const QJsonObject &d);
    QJsonObject cmdSpectrumGet(const QJsonObject &d);
    QJsonObject cmdTxSend(const QJsonObject &d);
    QJsonObject cmdTxQueueGet(const QJsonObject &d);
    QJsonObject cmdTxQueueClear(const QJsonObject &d);
    QJsonObject cmdTxHb(const QJsonObject &d);
    QJsonObject cmdTxSnrQuery(const QJsonObject &d);
    QJsonObject cmdTxInfoQuery(const QJsonObject &d);
    QJsonObject cmdTxStatusQuery(const QJsonObject &d);

    QJsonObject buildStatusObject() const;
    static QString messageTypeName(JS8Message::Type t);

    QWebSocketServer *m_srv;
    MainWindow       *m_app;
    QList<QWebSocket *> m_clients;

    // Throttle spectrum pushes (store latest; push every ~200 ms)
    QTimer           *m_specTimer   = nullptr;
    std::vector<float> m_latestBins;
    float              m_latestRate  = 12000.0f;
    bool               m_specDirty   = false;
};
