// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call WebSocket API server
#include "wsserver.h"
#include "mainwindow.h"
#include "config.h"
#include "hamlibcontroller.h"
#include "audioinput.h"
#include "audiooutput.h"
#include "messagemodel.h"
#include "js8message.h"

#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
// ── Construction ─────────────────────────────────────────────────────────────

WsServer::WsServer(MainWindow *app, QObject *parent)
    : QObject(parent)
    , m_srv(new QWebSocketServer(
          QStringLiteral("JF8Call"), QWebSocketServer::NonSecureMode, this))
    , m_app(app)
{
    connect(m_srv, &QWebSocketServer::newConnection,
            this, &WsServer::onNewConnection);

    // Throttle spectrum: accumulate updates, push at ~5 Hz
    m_specTimer = new QTimer(this);
    m_specTimer->setInterval(200);
    m_specTimer->setSingleShot(false);
    connect(m_specTimer, &QTimer::timeout, this, [this]() {
        if (!m_specDirty || m_clients.isEmpty()) return;
        m_specDirty = false;

        QJsonArray bins;
        const float hzPerBin = m_latestRate / (2.0f * static_cast<float>(m_latestBins.size()));
        for (float v : m_latestBins)
            bins.append(static_cast<double>(v));

        QJsonObject d;
        d[QStringLiteral("bins")]         = bins;
        d[QStringLiteral("bin_count")]    = static_cast<int>(m_latestBins.size());
        d[QStringLiteral("hz_per_bin")]   = static_cast<double>(hzPerBin);
        d[QStringLiteral("sample_rate")]  = static_cast<double>(m_latestRate);
        broadcast(QStringLiteral("spectrum"), d);
    });
    m_specTimer->start();
}

WsServer::~WsServer()
{
    stopListening();
}

// ── Listen / stop ─────────────────────────────────────────────────────────────

bool WsServer::listen(quint16 port, const QString &host)
{
    const QHostAddress addr = (host == QStringLiteral("0.0.0.0"))
        ? QHostAddress::AnyIPv4
        : QHostAddress(host.isEmpty() ? QStringLiteral("127.0.0.1") : host);
    if (!m_srv->listen(addr, port)) {
        qWarning() << "WsServer: failed to listen on"
                   << addr.toString() << "port" << port
                   << ":" << m_srv->errorString();
        return false;
    }
    return true;
}

void WsServer::stopListening()
{
    for (QWebSocket *c : m_clients) {
        c->close();
        c->deleteLater();
    }
    m_clients.clear();
    m_srv->close();
}

bool WsServer::isListening() const { return m_srv->isListening(); }
quint16 WsServer::port() const { return m_srv->serverPort(); }

// ── New connection ─────────────────────────────────────────────────────────────

void WsServer::onNewConnection()
{
    QWebSocket *client = m_srv->nextPendingConnection();
    if (!client) return;

    m_clients.append(client);
    connect(client, &QWebSocket::textMessageReceived,
            this, &WsServer::onTextMessage);
    connect(client, &QWebSocket::disconnected,
            this, &WsServer::onDisconnected);

    // Send a welcome / status push on connect
    QTimer::singleShot(0, this, [this, client]() {
        if (!m_clients.contains(client)) return;
        QJsonObject d;
        d[QStringLiteral("message")] = QStringLiteral("JF8Call WebSocket API");
        d[QStringLiteral("version")] = QStringLiteral("1.0");
        QJsonObject hello;
        hello[QStringLiteral("type")]  = QStringLiteral("hello");
        hello[QStringLiteral("data")]  = d;
        send(client, hello);
        // Immediately push current status
        send(client, buildStatusObject());
    });
}

void WsServer::onDisconnected()
{
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (client) {
        m_clients.removeAll(client);
        client->deleteLater();
    }
}

// ── Incoming message dispatch ─────────────────────────────────────────────────

void WsServer::onTextMessage(const QString &text)
{
    QWebSocket *client = qobject_cast<QWebSocket *>(sender());
    if (!client) return;

    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        reply(client, QString(), false, {}, QStringLiteral("invalid JSON"));
        return;
    }

    const QJsonObject msg = doc.object();
    const QString type = msg.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("cmd")) {
        reply(client, QString(), false, {}, QStringLiteral("expected type=cmd"));
        return;
    }

    handleCommand(client, msg);
}

void WsServer::handleCommand(QWebSocket *client, const QJsonObject &msg)
{
    const QString id  = msg.value(QStringLiteral("id")).toString();
    const QString cmd = msg.value(QStringLiteral("cmd")).toString();
    const QJsonObject d = msg.value(QStringLiteral("data")).toObject();

    // Dispatch table
    using Handler = QJsonObject (WsServer::*)(const QJsonObject &);
    static const QHash<QString, Handler> kHandlers = {
        { QStringLiteral("status.get"),           &WsServer::cmdStatusGet       },
        { QStringLiteral("config.get"),           &WsServer::cmdConfigGet       },
        { QStringLiteral("config.set"),           &WsServer::cmdConfigSet       },
        { QStringLiteral("audio.devices"),        &WsServer::cmdAudioDevices    },
        { QStringLiteral("audio.restart"),        &WsServer::cmdAudioRestart    },
        { QStringLiteral("radio.get"),            &WsServer::cmdRadioGet        },
        { QStringLiteral("radio.connect"),        &WsServer::cmdRadioConnect    },
        { QStringLiteral("radio.disconnect"),     &WsServer::cmdRadioDisconnect },
        { QStringLiteral("radio.frequency.set"),  &WsServer::cmdRadioFreqSet    },
        { QStringLiteral("radio.ptt.set"),        &WsServer::cmdRadioPttSet     },
        { QStringLiteral("radio.tune"),           &WsServer::cmdRadioTune       },
        { QStringLiteral("radio.power.get"),      &WsServer::cmdRadioPowerGet   },
        { QStringLiteral("radio.power.set"),      &WsServer::cmdRadioPowerSet   },
        { QStringLiteral("radio.volume.get"),     &WsServer::cmdRadioVolumeGet  },
        { QStringLiteral("radio.volume.set"),     &WsServer::cmdRadioVolumeSet  },
        { QStringLiteral("radio.mute"),           &WsServer::cmdRadioMute       },
        { QStringLiteral("radio.unmute"),         &WsServer::cmdRadioUnmute     },
        { QStringLiteral("messages.get"),         &WsServer::cmdMessagesGet     },
        { QStringLiteral("messages.clear"),       &WsServer::cmdMessagesClear   },
        { QStringLiteral("spectrum.get"),         &WsServer::cmdSpectrumGet     },
        { QStringLiteral("tx.send"),              &WsServer::cmdTxSend          },
        { QStringLiteral("tx.queue.get"),         &WsServer::cmdTxQueueGet      },
        { QStringLiteral("tx.queue.clear"),       &WsServer::cmdTxQueueClear    },
        { QStringLiteral("tx.hb"),                &WsServer::cmdTxHb            },
        { QStringLiteral("tx.snr"),               &WsServer::cmdTxSnrQuery      },
        { QStringLiteral("tx.info"),              &WsServer::cmdTxInfoQuery     },
        { QStringLiteral("tx.status"),            &WsServer::cmdTxStatusQuery   },
    };

    auto it = kHandlers.find(cmd);
    if (it == kHandlers.end()) {
        reply(client, id, false, {}, QStringLiteral("unknown command: ") + cmd);
        return;
    }

    try {
        QJsonObject result = (this->**it)(d);
        reply(client, id, true, result);
    } catch (const QString &e) {
        reply(client, id, false, {}, e);
    } catch (const std::exception &e) {
        reply(client, id, false, {}, QString::fromLatin1(e.what()));
    }
}

// ── Transport helpers ─────────────────────────────────────────────────────────

void WsServer::send(QWebSocket *client, const QJsonObject &msg)
{
    client->sendTextMessage(
        QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void WsServer::reply(QWebSocket *client, const QString &id,
                     bool ok, const QJsonObject &data, const QString &error)
{
    QJsonObject r;
    r[QStringLiteral("type")] = QStringLiteral("reply");
    if (!id.isEmpty())
        r[QStringLiteral("id")] = id;
    r[QStringLiteral("ok")] = ok;
    if (ok && !data.isEmpty())
        r[QStringLiteral("data")] = data;
    if (!ok)
        r[QStringLiteral("error")] = error;
    send(client, r);
}

void WsServer::broadcast(const QString &event, const QJsonObject &data)
{
    if (m_clients.isEmpty()) return;
    QJsonObject msg;
    msg[QStringLiteral("type")]  = QStringLiteral("event");
    msg[QStringLiteral("event")] = event;
    if (!data.isEmpty())
        msg[QStringLiteral("data")] = data;
    const QString text = QString::fromUtf8(
        QJsonDocument(msg).toJson(QJsonDocument::Compact));
    for (QWebSocket *c : m_clients)
        c->sendTextMessage(text);
}

// ── Push events ───────────────────────────────────────────────────────────────

void WsServer::pushMessageDecoded(const JS8Message &msg)
{
    QJsonObject d;
    d[QStringLiteral("time")]     = msg.utc.toString(QStringLiteral("HH:mm:ss"));
    d[QStringLiteral("utc_iso")]  = msg.utc.toString(Qt::ISODate);
    d[QStringLiteral("freq_hz")]  = static_cast<double>(msg.audioFreqHz);
    d[QStringLiteral("snr_db")]   = msg.snrDb;
    d[QStringLiteral("submode")]  = msg.submodeEnum;
    d[QStringLiteral("submode_name")] = msg.submodeStr;
    d[QStringLiteral("from")]     = msg.from;
    d[QStringLiteral("to")]       = msg.to;
    d[QStringLiteral("body")]     = msg.body;
    d[QStringLiteral("raw")]      = msg.rawText;
    d[QStringLiteral("type")]     = static_cast<int>(msg.type);
    d[QStringLiteral("type_name")] = messageTypeName(msg.type);
    broadcast(QStringLiteral("message.decoded"), d);
}

void WsServer::pushMessageFrame(float freqHz, int snrDb, int submode, int modemType,
                                int frameType, const QString &frameText,
                                const QString &assembledText, const QDateTime &utc)
{
    QJsonObject d;
    d[QStringLiteral("time")]           = utc.toString(QStringLiteral("HH:mm:ss"));
    d[QStringLiteral("utc_iso")]        = utc.toString(Qt::ISODate);
    d[QStringLiteral("freq_hz")]        = static_cast<double>(freqHz);
    d[QStringLiteral("snr_db")]         = snrDb;
    d[QStringLiteral("submode")]        = submode;
    d[QStringLiteral("submode_name")]   = submodeName(submode, modemType);
    d[QStringLiteral("frame_type")]     = frameType;
    d[QStringLiteral("is_complete")]    = false;
    d[QStringLiteral("frame_text")]     = frameText;
    d[QStringLiteral("assembled_text")] = assembledText;
    broadcast(QStringLiteral("message.frame"), d);
}

void WsServer::pushSpectrum(const std::vector<float> &bins, float sampleRateHz)
{
    m_latestBins  = bins;
    m_latestRate  = sampleRateHz;
    m_specDirty   = true;
    // Actual push happens in m_specTimer's timeout slot at ~5 Hz
}

QJsonObject WsServer::buildStatusObject() const
{
    const Config &c = m_app->apiConfig();
    QJsonObject d;
    d[QStringLiteral("callsign")]                  = c.callsign;
    d[QStringLiteral("grid")]                      = c.grid;
    d[QStringLiteral("submode")]                   = c.submode;
    d[QStringLiteral("submode_name")]              = submodeName(c.submode);
    d[QStringLiteral("frequency_khz")]             = c.frequencyKhz;
    d[QStringLiteral("tx_freq_hz")]                = c.txFreqHz;
    d[QStringLiteral("transmitting")]              = m_app->apiIsTransmitting();
    d[QStringLiteral("audio_running")]             = m_app->apiIsAudioRunning();
    d[QStringLiteral("radio_connected")]           = m_app->apiIsRadioConnected();
    d[QStringLiteral("radio_freq_khz")]            = m_app->apiRadioFreqKhz();
    d[QStringLiteral("radio_mode")]                = m_app->apiRadioMode();
    d[QStringLiteral("tx_queue_size")]             = m_app->apiTxQueueSize();
    d[QStringLiteral("heartbeat_enabled")]         = c.heartbeatEnabled;
    d[QStringLiteral("heartbeat_interval_periods")]= c.heartbeatIntervalPeriods;
    d[QStringLiteral("auto_reply")]                = c.autoReply;
    d[QStringLiteral("ws_port")]                   = static_cast<int>(port());
    d[QStringLiteral("ws_clients")]                = m_clients.size();
    QJsonObject r;
    r[QStringLiteral("type")]  = QStringLiteral("event");
    r[QStringLiteral("event")] = QStringLiteral("status");
    r[QStringLiteral("data")]  = d;
    return r;
}

void WsServer::pushStatus()
{
    if (m_clients.isEmpty()) return;
    const QJsonObject msg = buildStatusObject();
    const QString text = QString::fromUtf8(
        QJsonDocument(msg).toJson(QJsonDocument::Compact));
    for (QWebSocket *c : m_clients)
        c->sendTextMessage(text);
}

void WsServer::pushTxStarted()
{
    broadcast(QStringLiteral("tx.started"));
}

void WsServer::pushTxFinished()
{
    broadcast(QStringLiteral("tx.finished"));
}

void WsServer::pushRadioConnected(double khz, const QString &mode)
{
    QJsonObject d;
    d[QStringLiteral("freq_khz")] = khz;
    d[QStringLiteral("mode")]     = mode;
    broadcast(QStringLiteral("radio.connected"), d);
}

void WsServer::pushRadioDisconnected()
{
    broadcast(QStringLiteral("radio.disconnected"));
}

void WsServer::pushConfigChanged()
{
    const Config &c = m_app->apiConfig();
    QJsonObject d;
    d[QStringLiteral("callsign")]                   = c.callsign;
    d[QStringLiteral("grid")]                       = c.grid;
    d[QStringLiteral("modem_type")]                 = c.modemType;
    d[QStringLiteral("submode")]                    = c.submode;
    d[QStringLiteral("frequency_khz")]              = c.frequencyKhz;
    d[QStringLiteral("tx_freq_hz")]                 = c.txFreqHz;
    d[QStringLiteral("heartbeat_enabled")]          = c.heartbeatEnabled;
    d[QStringLiteral("heartbeat_interval_periods")] = c.heartbeatIntervalPeriods;
    d[QStringLiteral("auto_reply")]                 = c.autoReply;
    broadcast(QStringLiteral("config.changed"), d);
}

void WsServer::pushTxQueued(int queueSize)
{
    QJsonObject d;
    d[QStringLiteral("queue_size")] = queueSize;
    broadcast(QStringLiteral("tx.queued"), d);
}

void WsServer::pushError(const QString &msg)
{
    QJsonObject d;
    d[QStringLiteral("message")] = msg;
    broadcast(QStringLiteral("error"), d);
}

// ── Command handlers ──────────────────────────────────────────────────────────

QJsonObject WsServer::cmdStatusGet(const QJsonObject &)
{
    return buildStatusObject()[QStringLiteral("data")].toObject();
}

QJsonObject WsServer::cmdConfigGet(const QJsonObject &)
{
    const Config &c = m_app->apiConfig();
    QJsonObject d;
    d[QStringLiteral("callsign")]                   = c.callsign;
    d[QStringLiteral("grid")]                       = c.grid;
    d[QStringLiteral("audioInputName")]             = c.audioInputName;
    d[QStringLiteral("audioOutputName")]            = c.audioOutputName;
    d[QStringLiteral("modemType")]                  = c.modemType;
    d[QStringLiteral("submode")]                    = c.submode;
    d[QStringLiteral("frequencyKhz")]               = c.frequencyKhz;
    d[QStringLiteral("txFreqHz")]                   = c.txFreqHz;
    d[QStringLiteral("txPowerPct")]                 = c.txPowerPct;
    d[QStringLiteral("heartbeatEnabled")]           = c.heartbeatEnabled;
    d[QStringLiteral("heartbeatIntervalPeriods")]   = c.heartbeatIntervalPeriods;
    d[QStringLiteral("autoReply")]                  = c.autoReply;
    d[QStringLiteral("stationInfo")]                = c.stationInfo;
    d[QStringLiteral("stationStatus")]              = c.stationStatus;
    d[QStringLiteral("cqMessage")]                  = c.cqMessage;
    d[QStringLiteral("distMiles")]                  = c.distMiles;
    d[QStringLiteral("autoAtu")]                    = c.autoAtu;
    d[QStringLiteral("rigModel")]     = c.rigModel;
    d[QStringLiteral("rigPort")]      = c.rigPort;
    d[QStringLiteral("rigBaud")]      = c.rigBaud;
    d[QStringLiteral("rigDataBits")]  = c.rigDataBits;
    d[QStringLiteral("rigStopBits")]  = c.rigStopBits;
    d[QStringLiteral("rigParity")]    = c.rigParity;
    d[QStringLiteral("rigHandshake")] = c.rigHandshake;
    d[QStringLiteral("rigDtrState")]  = c.rigDtrState;
    d[QStringLiteral("rigRtsState")]  = c.rigRtsState;
    d[QStringLiteral("pttType")]      = c.pttType;
    d[QStringLiteral("pskReporterEnabled")]          = c.pskReporterEnabled;
    d[QStringLiteral("wsEnabled")]                  = c.wsEnabled;
    d[QStringLiteral("wsPort")]                     = c.wsPort;
    return d;
}

QJsonObject WsServer::cmdConfigSet(const QJsonObject &d)
{
    if (d.contains(QStringLiteral("callsign")))
        m_app->apiSetCallsign(d[QStringLiteral("callsign")].toString());
    if (d.contains(QStringLiteral("grid")))
        m_app->apiSetGrid(d[QStringLiteral("grid")].toString());
    if (d.contains(QStringLiteral("modemType")))
        m_app->apiSetModem(d[QStringLiteral("modemType")].toInt());
    if (d.contains(QStringLiteral("submode")))
        m_app->apiSetSubmode(d[QStringLiteral("submode")].toInt());
    if (d.contains(QStringLiteral("frequencyKhz")))
        m_app->apiSetFrequencyKhz(d[QStringLiteral("frequencyKhz")].toDouble());
    if (d.contains(QStringLiteral("txFreqHz")))
        m_app->apiSetTxFreqHz(d[QStringLiteral("txFreqHz")].toDouble());
    if (d.contains(QStringLiteral("heartbeatEnabled")))
        m_app->apiSetHeartbeatEnabled(d[QStringLiteral("heartbeatEnabled")].toBool());
    if (d.contains(QStringLiteral("heartbeatIntervalPeriods")))
        m_app->apiSetHeartbeatInterval(d[QStringLiteral("heartbeatIntervalPeriods")].toInt());
    if (d.contains(QStringLiteral("autoReply")))
        m_app->apiSetAutoReply(d[QStringLiteral("autoReply")].toBool());
    if (d.contains(QStringLiteral("audioInputName")))
        m_app->apiSetAudioInput(d[QStringLiteral("audioInputName")].toString());
    if (d.contains(QStringLiteral("audioOutputName")))
        m_app->apiSetAudioOutput(d[QStringLiteral("audioOutputName")].toString());
    if (d.contains(QStringLiteral("stationInfo")))
        m_app->apiSetStationInfo(d[QStringLiteral("stationInfo")].toString());
    if (d.contains(QStringLiteral("stationStatus")))
        m_app->apiSetStationStatus(d[QStringLiteral("stationStatus")].toString());
    if (d.contains(QStringLiteral("cqMessage")))
        m_app->apiSetCqMessage(d[QStringLiteral("cqMessage")].toString());
    if (d.contains(QStringLiteral("distMiles")))
        m_app->apiSetDistMiles(d[QStringLiteral("distMiles")].toBool());
    if (d.contains(QStringLiteral("autoAtu")))
        m_app->apiSetAutoAtu(d[QStringLiteral("autoAtu")].toBool());
    if (d.contains(QStringLiteral("pskReporterEnabled")))
        m_app->apiSetPskReporterEnabled(d[QStringLiteral("pskReporterEnabled")].toBool());

    // Rig config: update only fields present in the request
    const Config &cur = m_app->apiConfig();
    if (d.contains(QStringLiteral("rigModel"))    || d.contains(QStringLiteral("rigPort"))    ||
        d.contains(QStringLiteral("rigBaud"))     || d.contains(QStringLiteral("rigDataBits"))  ||
        d.contains(QStringLiteral("rigStopBits")) || d.contains(QStringLiteral("rigParity"))   ||
        d.contains(QStringLiteral("rigHandshake"))|| d.contains(QStringLiteral("rigDtrState")) ||
        d.contains(QStringLiteral("rigRtsState")) || d.contains(QStringLiteral("pttType"))) {
        RigConfig cfg;
        cfg.rigModel  = d.value(QStringLiteral("rigModel")).toInt(cur.rigModel);
        cfg.port      = d.value(QStringLiteral("rigPort")).toString(cur.rigPort);
        cfg.baudRate  = d.value(QStringLiteral("rigBaud")).toInt(cur.rigBaud);
        cfg.dataBits  = d.value(QStringLiteral("rigDataBits")).toInt(cur.rigDataBits);
        cfg.stopBits  = d.value(QStringLiteral("rigStopBits")).toInt(cur.rigStopBits);
        cfg.parity    = d.value(QStringLiteral("rigParity")).toInt(cur.rigParity);
        cfg.handshake = d.value(QStringLiteral("rigHandshake")).toInt(cur.rigHandshake);
        cfg.dtrState  = d.value(QStringLiteral("rigDtrState")).toInt(cur.rigDtrState);
        cfg.rtsState  = d.value(QStringLiteral("rigRtsState")).toInt(cur.rigRtsState);
        cfg.pttType   = d.value(QStringLiteral("pttType")).toInt(cur.pttType);
        m_app->apiSetRigConfig(cfg);
    }

    pushConfigChanged();
    return cmdConfigGet({});
}

QJsonObject WsServer::cmdAudioDevices(const QJsonObject &)
{
    QJsonArray inputs, outputs;
    for (const auto &n : AudioInput::availableDevices())  inputs.append(n);
    for (const auto &n : AudioOutput::availableDevices()) outputs.append(n);
    QJsonObject d;
    d[QStringLiteral("input")]  = inputs;
    d[QStringLiteral("output")] = outputs;
    return d;
}

QJsonObject WsServer::cmdAudioRestart(const QJsonObject &)
{
    m_app->apiRestartAudio();
    return {};
}

QJsonObject WsServer::cmdRadioGet(const QJsonObject &)
{
    const Config &c = m_app->apiConfig();
    QJsonObject d;
    d[QStringLiteral("connected")]  = m_app->apiIsRadioConnected();
    d[QStringLiteral("freq_khz")]   = m_app->apiRadioFreqKhz();
    d[QStringLiteral("mode")]       = m_app->apiRadioMode();
    d[QStringLiteral("rig_model")]   = c.rigModel;
    d[QStringLiteral("port")]        = c.rigPort;
    d[QStringLiteral("baud")]        = c.rigBaud;
    d[QStringLiteral("data_bits")]   = c.rigDataBits;
    d[QStringLiteral("stop_bits")]   = c.rigStopBits;
    d[QStringLiteral("parity")]      = c.rigParity;
    d[QStringLiteral("handshake")]   = c.rigHandshake;
    d[QStringLiteral("dtr_state")]   = c.rigDtrState;
    d[QStringLiteral("rts_state")]   = c.rigRtsState;
    d[QStringLiteral("ptt_type")]    = c.pttType;
    return d;
}

QJsonObject WsServer::cmdRadioConnect(const QJsonObject &d)
{
    const Config &cur = m_app->apiConfig();
    RigConfig cfg;
    cfg.rigModel  = d.value(QStringLiteral("rig_model")).toInt(cur.rigModel);
    cfg.port      = d.value(QStringLiteral("port")).toString(cur.rigPort);
    cfg.baudRate  = d.value(QStringLiteral("baud")).toInt(cur.rigBaud);
    cfg.dataBits  = d.value(QStringLiteral("data_bits")).toInt(cur.rigDataBits);
    cfg.stopBits  = d.value(QStringLiteral("stop_bits")).toInt(cur.rigStopBits);
    cfg.parity    = d.value(QStringLiteral("parity")).toInt(cur.rigParity);
    cfg.handshake = d.value(QStringLiteral("handshake")).toInt(cur.rigHandshake);
    cfg.dtrState  = d.value(QStringLiteral("dtr_state")).toInt(cur.rigDtrState);
    cfg.rtsState  = d.value(QStringLiteral("rts_state")).toInt(cur.rigRtsState);
    cfg.pttType   = d.value(QStringLiteral("ptt_type")).toInt(cur.pttType);
    m_app->apiConnectRadio(cfg);
    return {};
}

QJsonObject WsServer::cmdRadioDisconnect(const QJsonObject &)
{
    m_app->apiDisconnectRadio();
    return {};
}

QJsonObject WsServer::cmdRadioFreqSet(const QJsonObject &d)
{
    // Accept both "freq_khz" (documented) and legacy "khz"
    double khz = d.value(QStringLiteral("freq_khz")).toDouble(
                 d.value(QStringLiteral("khz")).toDouble(0));
    if (khz <= 0)
        throw QStringLiteral("freq_khz must be > 0");
    m_app->apiSetFrequency(khz);
    QJsonObject r;
    r[QStringLiteral("freq_khz")] = khz;
    return r;
}

QJsonObject WsServer::cmdRadioPttSet(const QJsonObject &d)
{
    // Accept both "ptt" (documented) and legacy "on"
    const bool on = d.contains(QStringLiteral("ptt"))
        ? d.value(QStringLiteral("ptt")).toBool(false)
        : d.value(QStringLiteral("on")).toBool(false);
    m_app->apiSetPtt(on);
    QJsonObject r;
    r[QStringLiteral("ptt")] = on;
    return r;
}

QJsonObject WsServer::cmdRadioTune(const QJsonObject &)
{
    if (!m_app->apiIsRadioConnected())
        throw QStringLiteral("radio not connected");
    m_app->apiTuneRadio();
    return {};
}

QJsonObject WsServer::cmdRadioPowerGet(const QJsonObject &)
{
    if (!m_app->apiIsRadioConnected())
        throw QStringLiteral("radio not connected");
    int pct = m_app->apiGetRfPower();
    if (pct < 0)
        throw QStringLiteral("RF power level not supported by this rig");
    QJsonObject r;
    r[QStringLiteral("power_pct")] = pct;
    return r;
}

QJsonObject WsServer::cmdRadioPowerSet(const QJsonObject &d)
{
    if (!m_app->apiIsRadioConnected())
        throw QStringLiteral("radio not connected");
    if (!d.contains(QStringLiteral("power_pct")))
        throw QStringLiteral("power_pct required (0-100)");
    int pct = d.value(QStringLiteral("power_pct")).toInt(-1);
    if (pct < 0 || pct > 100)
        throw QStringLiteral("power_pct must be 0-100");
    if (!m_app->apiSetRfPower(pct))
        throw QStringLiteral("Failed to set RF power");
    QJsonObject r;
    r[QStringLiteral("power_pct")] = pct;
    return r;
}

QJsonObject WsServer::cmdRadioVolumeGet(const QJsonObject &)
{
    if (!m_app->apiIsRadioConnected())
        throw QStringLiteral("radio not connected");
    int vol = m_app->apiGetAfVolume();
    if (vol < 0)
        throw QStringLiteral("AF volume not supported by this rig");
    QJsonObject r;
    r[QStringLiteral("volume")] = vol;
    return r;
}

QJsonObject WsServer::cmdRadioVolumeSet(const QJsonObject &d)
{
    if (!m_app->apiIsRadioConnected())
        throw QStringLiteral("radio not connected");
    if (!d.contains(QStringLiteral("volume")))
        throw QStringLiteral("volume required (0-100)");
    int vol = d.value(QStringLiteral("volume")).toInt(-1);
    if (vol < 0 || vol > 100)
        throw QStringLiteral("volume must be 0-100");
    if (!m_app->apiSetAfVolume(vol))
        throw QStringLiteral("Failed to set AF volume");
    QJsonObject r;
    r[QStringLiteral("volume")] = vol;
    return r;
}

QJsonObject WsServer::cmdRadioMute(const QJsonObject &)
{
    if (!m_app->apiIsRadioConnected())
        throw QStringLiteral("radio not connected");
    if (!m_app->apiSetMute(true))
        throw QStringLiteral("Failed to mute radio");
    QJsonObject r;
    r[QStringLiteral("muted")] = true;
    return r;
}

QJsonObject WsServer::cmdRadioUnmute(const QJsonObject &)
{
    if (!m_app->apiIsRadioConnected())
        throw QStringLiteral("radio not connected");
    if (!m_app->apiSetMute(false))
        throw QStringLiteral("Failed to unmute radio");
    QJsonObject r;
    r[QStringLiteral("muted")] = false;
    return r;
}

QJsonObject WsServer::cmdMessagesGet(const QJsonObject &d)
{
    int offset = d.value(QStringLiteral("offset")).toInt(0);
    int limit  = d.value(QStringLiteral("limit")).toInt(100);
    limit = qBound(1, limit, 1000);
    QJsonArray msgs = m_app->apiGetMessages(offset, limit);
    QJsonObject r;
    r[QStringLiteral("messages")] = msgs;
    r[QStringLiteral("count")]    = msgs.size();
    r[QStringLiteral("offset")]   = offset;
    r[QStringLiteral("limit")]    = limit;
    return r;
}

QJsonObject WsServer::cmdMessagesClear(const QJsonObject &)
{
    m_app->apiClearMessages();
    return {};
}

QJsonObject WsServer::cmdSpectrumGet(const QJsonObject &)
{
    const auto bins = m_app->apiLatestSpectrum();
    const float rate = m_app->apiSpectrumSampleRate();
    const float hzPerBin = bins.empty() ? 0.0f
        : rate / (2.0f * static_cast<float>(bins.size()));
    QJsonArray arr;
    for (float v : bins) arr.append(static_cast<double>(v));
    QJsonObject d;
    d[QStringLiteral("bins")]       = arr;
    d[QStringLiteral("bin_count")]  = static_cast<int>(bins.size());
    d[QStringLiteral("hz_per_bin")] = static_cast<double>(hzPerBin);
    d[QStringLiteral("sample_rate")]= static_cast<double>(rate);
    return d;
}

// ── TX commands ───────────────────────────────────────────────────────────────

// Returns a UI submode index.
// Gfsk8/JS8: 0=Normal, 1=Fast, 2=Turbo, 3=Slow, 4=Ultra
// Codec2 DATAC: 0=DATAC0, 1=DATAC1, 2=DATAC3
static int parseSubmode(const QString &s, int fallback)
{
    const QString lo = s.toLower();
    // JS8 named submodes
    if (lo == QLatin1String("normal") || lo == QLatin1String("a")) return 0;
    if (lo == QLatin1String("fast")   || lo == QLatin1String("b")) return 1;
    if (lo == QLatin1String("turbo")  || lo == QLatin1String("c")) return 2;
    if (lo == QLatin1String("slow")   || lo == QLatin1String("e")) return 3;
    if (lo == QLatin1String("ultra")  || lo == QLatin1String("i")) return 4;
    // Codec2 DATAC named submodes
    if (lo == QLatin1String("datac0")) return 0;
    if (lo == QLatin1String("datac1")) return 1;
    if (lo == QLatin1String("datac3")) return 2;
    // Numeric: treat as UI index
    bool ok;
    int v = lo.toInt(&ok);
    if (ok && v >= 0) return v;
    return fallback;
}

QJsonObject WsServer::cmdTxSend(const QJsonObject &d)
{
    const QString text = d.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty())
        throw QStringLiteral("text is required");

    if (d.contains(QStringLiteral("submode"))) {
        int sm = parseSubmode(
            d[QStringLiteral("submode")].toString(),
            m_app->apiConfig().submode);
        m_app->apiQueueTxSubmode(text, sm);
    } else {
        m_app->apiQueueTx(text);
    }
    QJsonObject r;
    r[QStringLiteral("queue_size")] = m_app->apiTxQueueSize();
    return r;
}

QJsonObject WsServer::cmdTxQueueGet(const QJsonObject &)
{
    QJsonArray arr;
    for (const QVariantMap &f : m_app->apiTxQueue()) {
        QJsonObject e;
        e[QStringLiteral("payload")]    = f[QStringLiteral("payload")].toString();
        e[QStringLiteral("frame_type")] = f[QStringLiteral("frameType")].toInt();
        e[QStringLiteral("submode")]    = f[QStringLiteral("submode")].toInt();
        arr.append(e);
    }
    QJsonObject r;
    r[QStringLiteral("queue")]      = arr;
    r[QStringLiteral("queue_size")] = arr.size();
    return r;
}

QJsonObject WsServer::cmdTxQueueClear(const QJsonObject &)
{
    m_app->apiClearTxQueue();
    return {};
}

QJsonObject WsServer::cmdTxHb(const QJsonObject &)
{
    const QString call = m_app->apiConfig().callsign;
    if (call.isEmpty())
        throw QStringLiteral("callsign not configured");
    m_app->apiQueueTx(call + QStringLiteral(" @HB"));
    QJsonObject r;
    r[QStringLiteral("queue_size")] = m_app->apiTxQueueSize();
    return r;
}

QJsonObject WsServer::cmdTxSnrQuery(const QJsonObject &d)
{
    const QString to   = d.value(QStringLiteral("to")).toString().trimmed().toUpper();
    const QString from = m_app->apiConfig().callsign.toUpper();
    if (to.isEmpty())   throw QStringLiteral("to is required");
    if (from.isEmpty()) throw QStringLiteral("callsign not configured");
    m_app->apiQueueTx(QStringLiteral("%1 %2 @SNR?").arg(to, from));
    QJsonObject r;
    r[QStringLiteral("queue_size")] = m_app->apiTxQueueSize();
    return r;
}

QJsonObject WsServer::cmdTxInfoQuery(const QJsonObject &d)
{
    const QString to   = d.value(QStringLiteral("to")).toString().trimmed().toUpper();
    const QString from = m_app->apiConfig().callsign.toUpper();
    if (to.isEmpty())   throw QStringLiteral("to is required");
    if (from.isEmpty()) throw QStringLiteral("callsign not configured");
    m_app->apiQueueTx(QStringLiteral("%1 %2 @INFO?").arg(to, from));
    QJsonObject r;
    r[QStringLiteral("queue_size")] = m_app->apiTxQueueSize();
    return r;
}

QJsonObject WsServer::cmdTxStatusQuery(const QJsonObject &d)
{
    const QString to   = d.value(QStringLiteral("to")).toString().trimmed().toUpper();
    const QString from = m_app->apiConfig().callsign.toUpper();
    if (to.isEmpty())   throw QStringLiteral("to is required");
    if (from.isEmpty()) throw QStringLiteral("callsign not configured");
    m_app->apiQueueTx(QStringLiteral("%1 %2 @?").arg(to, from));
    QJsonObject r;
    r[QStringLiteral("queue_size")] = m_app->apiTxQueueSize();
    return r;
}

// ── Private helpers ───────────────────────────────────────────────────────────

QString WsServer::messageTypeName(JS8Message::Type t)
{
    switch (t) {
        case JS8Message::Type::Heartbeat:        return QStringLiteral("Heartbeat");
        case JS8Message::Type::DirectedMessage:  return QStringLiteral("DirectedMessage");
        case JS8Message::Type::SnrQuery:         return QStringLiteral("SnrQuery");
        case JS8Message::Type::SnrReply:         return QStringLiteral("SnrReply");
        case JS8Message::Type::InfoQuery:        return QStringLiteral("InfoQuery");
        case JS8Message::Type::InfoReply:        return QStringLiteral("InfoReply");
        case JS8Message::Type::StatusQuery:      return QStringLiteral("StatusQuery");
        case JS8Message::Type::StatusReply:      return QStringLiteral("StatusReply");
        case JS8Message::Type::CompoundDirected: return QStringLiteral("CompoundDirected");
        default:                                 return QStringLiteral("Unknown");
    }
}
