// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * jf8-tool — JF8Call WebSocket API command-line tool
 *
 * Usage:
 *   jf8-tool [--host HOST] [--port PORT] <command> [args...]
 *
 * Commands:
 *   status                              Show current status
 *   config get                          Print full configuration
 *   config set key=val [key=val ...]    Set configuration fields
 *   audio devices                       List audio devices
 *   audio restart                       Restart audio I/O
 *   radio get                           Show radio status
 *   radio connect [--model N] [--port PATH] [--baud N] [--ptt N]
 *   radio disconnect                    Disconnect from rig
 *   radio freq KHZ                      Set VFO frequency (kHz)
 *   radio ptt on|off                    Set PTT
 *   radio tune                          Trigger ATU tune
 *   messages [--offset N] [--limit N]   View decoded messages
 *   messages clear                      Clear message log
 *   spectrum                            Show spectrum snapshot
 *   send "text" [--submode NAME]        Transmit a message
 *   tx hb                               Send heartbeat (@HB)
 *   tx snr CALLSIGN                     Send @SNR? query
 *   tx info CALLSIGN                    Send @INFO? query
 *   tx grid CALLSIGN                    Send @GRID? query
 *   tx status CALLSIGN                  Send @STATUS? query
 *   tx hearing CALLSIGN                 Send @HEARING? query
 *   tx queue                            Show TX queue
 *   tx clear                            Clear TX queue
 *   monitor [--filter EVENT]            Stream live events (Ctrl-C to stop)
 *   stream [--frames] [--json] [--output FILE]
 *                                       Stream decoded messages
 */

#include "jf8call_version.h"

#include <QCoreApplication>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QTimer>
#include <QUuid>
#include <QFile>
#include <QTextStream>
#include <QUrl>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// ── Signal handling ───────────────────────────────────────────────────────────

static void sigHandler(int) { QCoreApplication::quit(); }

// ── Parsed arguments ──────────────────────────────────────────────────────────

struct Args {
    QString host = QStringLiteral("localhost");
    int     port = 2102;

    QString cmd;   // top-level command
    QString sub;   // sub-command (get/set/devices/restart/connect/disconnect/...)

    // config set
    QStringList kv;

    // radio connect
    int     radioModel = -1;
    QString radioPort;
    int     radioBaud  = -1;
    int     radioPtt   = -1;

    // radio freq
    double radioFreqKhz = 0.0;

    // radio ptt
    QString radioState;   // "on" | "off"

    // messages
    int messagesOffset = 0;
    int messagesLimit  = 50;

    // send
    QString sendText;
    QString sendSubmode;

    // tx snr/info/grid/status/hearing
    QString txCallsign;

    // monitor
    QString monitorFilter;

    // stream
    bool    streamFrames = false;
    bool    streamJson   = false;
    QString streamOutput;
};

// ── Usage ─────────────────────────────────────────────────────────────────────

static void printUsage()
{
    std::cout <<
"Usage: jf8-tool [--host HOST] [--port PORT] <command> [args...]\n"
"\n"
"Commands:\n"
"  status                              Show current status\n"
"  config get                          Print full configuration\n"
"  config set key=val [key=val ...]    Set configuration fields\n"
"  audio devices                       List audio devices\n"
"  audio restart                       Restart audio I/O\n"
"  radio get                           Show radio status\n"
"  radio connect [--model N] [--port PATH] [--baud N] [--ptt N]\n"
"                                      Connect to rig (PTT: 0=VOX 1=CAT 2=DTR 3=RTS)\n"
"  radio disconnect                    Disconnect from rig\n"
"  radio freq KHZ                      Set VFO frequency (kHz)\n"
"  radio ptt on|off                    Set PTT\n"
"  radio tune                          Trigger ATU tune\n"
"  messages [--offset N] [--limit N]   View decoded messages (default limit: 50)\n"
"  messages clear                      Clear message log\n"
"  spectrum                            Show spectrum snapshot\n"
"  send \"text\" [--submode NAME]        Transmit a message\n"
"                                      Submodes: normal fast turbo slow ultra\n"
"  tx hb                               Send heartbeat (@HB)\n"
"  tx snr CALLSIGN                     Send @SNR? query\n"
"  tx info CALLSIGN                    Send @INFO? query\n"
"  tx grid CALLSIGN                    Send @GRID? query\n"
"  tx status CALLSIGN                  Send @STATUS? query\n"
"  tx hearing CALLSIGN                 Send @HEARING? query\n"
"  tx queue                            Show TX queue\n"
"  tx clear                            Clear TX queue\n"
"  monitor [--filter EVENT]            Stream all live events (Ctrl-C to stop)\n"
"  stream [--frames] [--json]          Stream decoded messages (Ctrl-C to stop)\n"
"         [--output FILE]\n"
"\n"
"Options:\n"
"  --host HOST    JF8Call host (default: localhost)\n"
"  --port PORT    WebSocket port (default: 2102)\n"
"  --version      Show version and exit\n"
"  --help         Show this help\n"
"\n"
"Examples:\n"
"  jf8-tool status\n"
"  jf8-tool config set callsign=W5XYZ grid=DM79AA\n"
"  jf8-tool send \"W4ABC HELLO\" --submode fast\n"
"  jf8-tool radio connect --model 3073 --port /dev/ttyUSB0 --baud 9600 --ptt 1\n"
"  jf8-tool radio freq 14078.0\n"
"  jf8-tool messages --limit 20\n"
"  jf8-tool monitor --filter message.decoded\n"
"  jf8-tool stream --output /tmp/rx.log\n"
    << std::flush;
}

// ── Argument parser ───────────────────────────────────────────────────────────

// Returns true if parsing succeeded and the tool should connect.
// Returns false and sets exitCode when we should exit immediately (--help,
// --version, or a parse error).
static bool parseArgs(int argc, char *argv[], Args &out, int &exitCode)
{
    std::vector<std::string> av(argv, argv + argc);
    exitCode = 0;
    size_t i = 1; // skip program name

    // ── Global options ──
    while (i < av.size()) {
        const std::string &a = av[i];
        if (a == "--host" && i + 1 < av.size()) {
            out.host = QString::fromStdString(av[++i]);
        } else if (a.rfind("--host=", 0) == 0) {
            out.host = QString::fromStdString(a.substr(7));
        } else if (a == "--port" && i + 1 < av.size()) {
            out.port = std::stoi(av[++i]);
        } else if (a.rfind("--port=", 0) == 0) {
            out.port = std::stoi(a.substr(7));
        } else if (a == "--version") {
            std::cout << "jf8-tool " JF8CALL_VERSION_STR "\n";
            exitCode = 0;
            return false;
        } else if (a == "--help" || a == "-h") {
            printUsage();
            exitCode = 0;
            return false;
        } else if (!a.empty() && a[0] != '-') {
            break; // command starts here
        } else {
            std::cerr << "jf8-tool: unknown option: " << a << "\n"
                      << "Run 'jf8-tool --help' for usage.\n";
            exitCode = 1;
            return false;
        }
        ++i;
    }

    if (i >= av.size()) {
        printUsage();
        exitCode = 1;
        return false;
    }

    out.cmd = QString::fromStdString(av[i++]);

    // ── status ──────────────────────────────────────────────────────────────
    if (out.cmd == "status") {
        // no arguments

    // ── config ──────────────────────────────────────────────────────────────
    } else if (out.cmd == "config") {
        if (i >= av.size()) {
            std::cerr << "config requires a subcommand: get, set\n";
            exitCode = 1; return false;
        }
        out.sub = QString::fromStdString(av[i++]);
        if (out.sub == "get") {
            // no arguments
        } else if (out.sub == "set") {
            while (i < av.size())
                out.kv.append(QString::fromStdString(av[i++]));
            if (out.kv.isEmpty()) {
                std::cerr << "config set requires at least one key=value pair\n";
                exitCode = 1; return false;
            }
        } else {
            std::cerr << "config: unknown subcommand '" << out.sub.toStdString() << "'\n";
            exitCode = 1; return false;
        }

    // ── audio ────────────────────────────────────────────────────────────────
    } else if (out.cmd == "audio") {
        if (i >= av.size()) {
            std::cerr << "audio requires a subcommand: devices, restart\n";
            exitCode = 1; return false;
        }
        out.sub = QString::fromStdString(av[i++]);
        if (out.sub != "devices" && out.sub != "restart") {
            std::cerr << "audio: unknown subcommand '" << out.sub.toStdString() << "'\n";
            exitCode = 1; return false;
        }

    // ── radio ────────────────────────────────────────────────────────────────
    } else if (out.cmd == "radio") {
        if (i >= av.size()) {
            std::cerr << "radio requires a subcommand: get, connect, disconnect, freq, ptt, tune\n";
            exitCode = 1; return false;
        }
        out.sub = QString::fromStdString(av[i++]);
        if (out.sub == "get" || out.sub == "disconnect" || out.sub == "tune") {
            // no arguments
        } else if (out.sub == "connect") {
            while (i < av.size()) {
                const std::string &a = av[i];
                if      (a == "--model"   && i+1 < av.size()) { out.radioModel = std::stoi(av[++i]); }
                else if (a.rfind("--model=",  0) == 0)        { out.radioModel = std::stoi(a.substr(8)); }
                else if (a == "--port"    && i+1 < av.size()) { out.radioPort  = QString::fromStdString(av[++i]); }
                else if (a.rfind("--port=",   0) == 0)        { out.radioPort  = QString::fromStdString(a.substr(7)); }
                else if (a == "--baud"    && i+1 < av.size()) { out.radioBaud  = std::stoi(av[++i]); }
                else if (a.rfind("--baud=",   0) == 0)        { out.radioBaud  = std::stoi(a.substr(7)); }
                else if (a == "--ptt"     && i+1 < av.size()) { out.radioPtt   = std::stoi(av[++i]); }
                else if (a.rfind("--ptt=",    0) == 0)        { out.radioPtt   = std::stoi(a.substr(6)); }
                else { std::cerr << "radio connect: unknown option: " << a << "\n"; exitCode=1; return false; }
                ++i;
            }
        } else if (out.sub == "freq") {
            if (i >= av.size()) { std::cerr << "radio freq requires KHZ argument\n"; exitCode=1; return false; }
            out.radioFreqKhz = std::stod(av[i++]);
        } else if (out.sub == "ptt") {
            if (i >= av.size()) { std::cerr << "radio ptt requires on|off\n"; exitCode=1; return false; }
            out.radioState = QString::fromStdString(av[i++]);
            if (out.radioState != "on" && out.radioState != "off") {
                std::cerr << "radio ptt: expected 'on' or 'off'\n"; exitCode=1; return false;
            }
        } else {
            std::cerr << "radio: unknown subcommand '" << out.sub.toStdString() << "'\n";
            exitCode=1; return false;
        }

    // ── messages ─────────────────────────────────────────────────────────────
    } else if (out.cmd == "messages") {
        while (i < av.size()) {
            const std::string &a = av[i];
            if      (a == "--offset"  && i+1 < av.size()) { out.messagesOffset = std::stoi(av[++i]); }
            else if (a.rfind("--offset=", 0) == 0)        { out.messagesOffset = std::stoi(a.substr(9)); }
            else if (a == "--limit"   && i+1 < av.size()) { out.messagesLimit  = std::stoi(av[++i]); }
            else if (a.rfind("--limit=",  0) == 0)        { out.messagesLimit  = std::stoi(a.substr(8)); }
            else if (a == "clear")                         { out.sub = QStringLiteral("clear"); }
            else { std::cerr << "messages: unknown option: " << a << "\n"; exitCode=1; return false; }
            ++i;
        }

    // ── spectrum ──────────────────────────────────────────────────────────────
    } else if (out.cmd == "spectrum") {
        // no arguments

    // ── send ──────────────────────────────────────────────────────────────────
    } else if (out.cmd == "send") {
        if (i >= av.size()) { std::cerr << "send requires a message text argument\n"; exitCode=1; return false; }
        out.sendText = QString::fromStdString(av[i++]);
        while (i < av.size()) {
            const std::string &a = av[i];
            if      (a == "--submode"  && i+1 < av.size()) { out.sendSubmode = QString::fromStdString(av[++i]); }
            else if (a.rfind("--submode=", 0) == 0)        { out.sendSubmode = QString::fromStdString(a.substr(10)); }
            else { std::cerr << "send: unknown option: " << a << "\n"; exitCode=1; return false; }
            ++i;
        }

    // ── tx ────────────────────────────────────────────────────────────────────
    } else if (out.cmd == "tx") {
        if (i >= av.size()) {
            std::cerr << "tx requires a subcommand: hb, snr, info, grid, status, hearing, queue, clear\n";
            exitCode=1; return false;
        }
        out.sub = QString::fromStdString(av[i++]);
        if (out.sub == "hb" || out.sub == "queue" || out.sub == "clear") {
            // no arguments
        } else if (out.sub == "snr"  || out.sub == "info" || out.sub == "grid"
                || out.sub == "status" || out.sub == "hearing") {
            if (i >= av.size()) {
                std::cerr << "tx " << out.sub.toStdString() << " requires CALLSIGN\n";
                exitCode=1; return false;
            }
            out.txCallsign = QString::fromStdString(av[i++]).toUpper();
        } else {
            std::cerr << "tx: unknown subcommand '" << out.sub.toStdString() << "'\n";
            exitCode=1; return false;
        }

    // ── monitor ───────────────────────────────────────────────────────────────
    } else if (out.cmd == "monitor") {
        while (i < av.size()) {
            const std::string &a = av[i];
            if      (a == "--filter"  && i+1 < av.size()) { out.monitorFilter = QString::fromStdString(av[++i]); }
            else if (a.rfind("--filter=", 0) == 0)        { out.monitorFilter = QString::fromStdString(a.substr(9)); }
            else { std::cerr << "monitor: unknown option: " << a << "\n"; exitCode=1; return false; }
            ++i;
        }

    // ── stream ────────────────────────────────────────────────────────────────
    } else if (out.cmd == "stream") {
        while (i < av.size()) {
            const std::string &a = av[i];
            if      (a == "--frames")                         { out.streamFrames = true; }
            else if (a == "--json")                           { out.streamJson   = true; }
            else if (a == "--output"  && i+1 < av.size())    { out.streamOutput = QString::fromStdString(av[++i]); }
            else if (a.rfind("--output=", 0) == 0)           { out.streamOutput = QString::fromStdString(a.substr(9)); }
            else { std::cerr << "stream: unknown option: " << a << "\n"; exitCode=1; return false; }
            ++i;
        }

    } else {
        std::cerr << "jf8-tool: unknown command '" << out.cmd.toStdString() << "'\n"
                  << "Run 'jf8-tool --help' for usage.\n";
        exitCode = 1;
        return false;
    }

    return true;
}

// ── Main tool class ───────────────────────────────────────────────────────────

class Jf8Tool : public QObject
{
    Q_OBJECT

public:
    explicit Jf8Tool(Args args, QObject *parent = nullptr)
        : QObject(parent)
        , m_args(std::move(args))
        , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    {
        connect(m_ws, &QWebSocket::connected,
                this, &Jf8Tool::onConnected);
        connect(m_ws, &QWebSocket::textMessageReceived,
                this, &Jf8Tool::onMessage);
        connect(m_ws, &QWebSocket::disconnected,
                this, &Jf8Tool::onDisconnected);
        connect(m_ws, &QWebSocket::errorOccurred,
                this, &Jf8Tool::onSocketError);
    }


public slots:
    void start()
    {
        QUrl url(QString("ws://%1:%2").arg(m_args.host).arg(m_args.port));
        m_ws->open(url);

        // Timeout for initial connection + drain
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        m_timer->setInterval(10000);
        connect(m_timer, &QTimer::timeout, this, &Jf8Tool::onTimeout);
        m_timer->start();
    }

private slots:
    void onConnected()
    {
        // Timer stays running: guards against server sending <2 initial messages
    }

    void onMessage(const QString &text)
    {
        // Drain the two initial messages JF8Call sends on connect:
        //   1. hello event
        //   2. status event
        if (m_drainCount < 2) {
            ++m_drainCount;
            if (m_drainCount == 2) {
                m_timer->stop();
                dispatch();
            }
            return;
        }

        handleMessage(text);
    }

    void onDisconnected()
    {
        if (!m_doneCalled) {
            // Only an error if we were mid-command, not after a clean done()
            std::cerr << "Connection closed unexpectedly.\n";
            emitFinished(1);
        }
    }

    void onSocketError(QAbstractSocket::SocketError)
    {
        std::cerr << "Cannot connect to JF8Call at ws://"
                  << m_args.host.toStdString() << ":" << m_args.port
                  << " — " << m_ws->errorString().toStdString() << "\n";
        emitFinished(1);
    }

    void onTimeout()
    {
        if (m_drainCount < 2)
            std::cerr << "Timed out waiting for initial messages from JF8Call.\n";
        else
            std::cerr << "Timed out waiting for reply (10 s).\n";
        emitFinished(1);
    }

private:
    // ── Dispatch ─────────────────────────────────────────────────────────────

    void dispatch()
    {
        const QString &cmd = m_args.cmd;
        const QString &sub = m_args.sub;

        if (cmd == "status") {
            sendCmd("status.get");

        } else if (cmd == "config") {
            if (sub == "get") {
                sendCmd("config.get");
            } else {
                QJsonObject data;
                for (const QString &kv : m_args.kv) {
                    int eq = kv.indexOf('=');
                    if (eq < 0) {
                        std::cerr << "Bad key=value pair: " << kv.toStdString() << "\n";
                        emitFinished(1); return;
                    }
                    QString k = kv.left(eq);
                    QString v = kv.mid(eq + 1);
                    // Coerce type: bool → int → double → string
                    if      (v.toLower() == "true")  data[k] = true;
                    else if (v.toLower() == "false") data[k] = false;
                    else {
                        bool ok;
                        int iv = v.toInt(&ok);
                        if (ok) { data[k] = iv; continue; }
                        double dv = v.toDouble(&ok);
                        if (ok) { data[k] = dv; continue; }
                        data[k] = v;
                    }
                }
                sendCmd("config.set", data);
            }

        } else if (cmd == "audio") {
            sendCmd(sub == "devices" ? "audio.devices" : "audio.restart");

        } else if (cmd == "radio") {
            if      (sub == "get")        sendCmd("radio.get");
            else if (sub == "disconnect") sendCmd("radio.disconnect");
            else if (sub == "tune")       sendCmd("radio.tune");
            else if (sub == "connect") {
                QJsonObject data;
                if (m_args.radioModel >= 0)   data["rig_model"] = m_args.radioModel;
                if (!m_args.radioPort.isEmpty()) data["port"]   = m_args.radioPort;
                if (m_args.radioBaud  >= 0)   data["baud"]      = m_args.radioBaud;
                if (m_args.radioPtt   >= 0)   data["ptt_type"]  = m_args.radioPtt;
                sendCmd("radio.connect", data);
            } else if (sub == "freq") {
                sendCmd("radio.frequency.set", QJsonObject{{"freq_khz", m_args.radioFreqKhz}});
            } else if (sub == "ptt") {
                bool ptt = (m_args.radioState == "on");
                sendCmd("radio.ptt.set", QJsonObject{{"ptt", ptt}});
            }

        } else if (cmd == "messages") {
            if (sub == "clear") {
                sendCmd("messages.clear");
            } else {
                QJsonObject data{
                    {"offset", m_args.messagesOffset},
                    {"limit",  m_args.messagesLimit}
                };
                sendCmd("messages.get", data);
            }

        } else if (cmd == "spectrum") {
            sendCmd("spectrum.get");

        } else if (cmd == "send") {
            QJsonObject data{{"text", m_args.sendText}};
            if (!m_args.sendSubmode.isEmpty()) data["submode"] = m_args.sendSubmode;
            sendCmd("tx.send", data);

        } else if (cmd == "tx") {
            if      (sub == "hb")      sendCmd("tx.hb");
            else if (sub == "snr")     sendCmd("tx.snr",    QJsonObject{{"to", m_args.txCallsign}});
            else if (sub == "info")    sendCmd("tx.info",   QJsonObject{{"to", m_args.txCallsign}});
            else if (sub == "grid")    sendCmd("tx.send",   QJsonObject{{"text", m_args.txCallsign + " @GRID?"}});
            else if (sub == "status")  sendCmd("tx.status", QJsonObject{{"to", m_args.txCallsign}});
            else if (sub == "hearing") sendCmd("tx.send",   QJsonObject{{"text", m_args.txCallsign + " @HEARING?"}});
            else if (sub == "queue")   sendCmd("tx.queue.get");
            else if (sub == "clear")   sendCmd("tx.queue.clear");

        } else if (cmd == "monitor" || cmd == "stream") {
            m_streaming = true;
            if (cmd == "stream" && !m_args.streamOutput.isEmpty()) {
                m_outFile = new QFile(m_args.streamOutput, this);
                if (!m_outFile->open(QIODevice::Append | QIODevice::Text)) {
                    std::cerr << "Cannot open output file: "
                              << m_args.streamOutput.toStdString() << "\n";
                    emitFinished(1); return;
                }
                m_outStream = new QTextStream(m_outFile);
            }
            if (cmd == "monitor") {
                printf("Monitoring events%s… (Ctrl-C to stop)\n",
                    m_args.monitorFilter.isEmpty() ? "" :
                    (" (filter: " + m_args.monitorFilter.toLower() + ")").toUtf8().constData());
                fflush(stdout);
            }
            // No reply timeout for streaming commands
        }
    }

    // ── Send a command ────────────────────────────────────────────────────────

    void sendCmd(const QString &apiCmd, const QJsonObject &data = {})
    {
        m_cmdId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        QJsonObject msg{{"type", "cmd"}, {"id", m_cmdId}, {"cmd", apiCmd}};
        if (!data.isEmpty()) msg["data"] = data;
        m_ws->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
        m_timer->start(10000); // restart reply timeout
    }

    // ── Route incoming messages ───────────────────────────────────────────────

    void handleMessage(const QString &text)
    {
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (!doc.isObject()) return;
        QJsonObject obj = doc.object();

        if (m_streaming) {
            handleStreamEvent(obj);
            return;
        }

        // One-shot: look for the matching reply
        if (obj["type"].toString() == "reply" && obj["id"].toString() == m_cmdId) {
            m_timer->stop();
            handleReply(obj);
            emitFinished(m_exitCode);
        }
        // All other events (status push, etc.) are silently ignored while waiting
    }

    // ── Print reply for one-shot commands ─────────────────────────────────────

    void handleReply(const QJsonObject &r)
    {
        bool ok = r["ok"].toBool();
        QJsonObject data = r["data"].toObject();
        const QString &cmd = m_args.cmd;
        const QString &sub = m_args.sub;

        auto errMsg = [&]() {
            fprintf(stderr, "Error: %s\n", r["error"].toString().toUtf8().constData());
            m_exitCode = 1;
        };

        auto okOrErr = [&]() {
            if (ok) printf("OK\n");
            else errMsg();
        };

        // ── status ──────────────────────────────────────────────────────────
        if (cmd == "status") {
            if (!ok) { errMsg(); return; }
            auto str = [&](const char *k) {
                return data[k].toString(QStringLiteral("—")).toUtf8().constData();
            };
            auto numStr = [&](const char *k) -> std::string {
                QJsonValue v = data[k];
                if (v.isDouble()) return std::to_string(v.toDouble());
                return v.toString("—").toStdString();
            };
            printf("Callsign   : %s\n",  str("callsign"));
            printf("Grid       : %s\n",  str("grid"));
            printf("Submode    : %s (%d)\n",
                data["submode_name"].toString("?").toUtf8().constData(),
                data["submode"].toInt());
            printf("Dial freq  : %s kHz\n", numStr("frequency_khz").c_str());
            printf("TX offset  : %s Hz\n",  numStr("tx_freq_hz").c_str());
            printf("Transmit   : %s\n",  data["transmitting"].toBool() ? "yes" : "no");
            printf("Audio      : %s\n",  data["audio_running"].toBool() ? "running" : "stopped");
            printf("Radio      : %s\n",  data["radio_connected"].toBool() ? "connected" : "not connected");
            if (data["radio_connected"].toBool()) {
                printf("Radio freq : %s kHz  %s\n",
                    numStr("radio_freq_khz").c_str(), str("radio_mode"));
            }
            printf("TX queue   : %d frame(s)\n",  data["tx_queue_size"].toInt());
            printf("Heartbeat  : %s  every %d period(s)\n",
                data["heartbeat_enabled"].toBool() ? "on" : "off",
                data["heartbeat_interval_periods"].toInt());
            printf("Auto-reply : %s\n",  data["auto_reply"].toBool() ? "on" : "off");
            printf("WS clients : %d\n",  data["ws_clients"].toInt());

        // ── config ──────────────────────────────────────────────────────────
        } else if (cmd == "config") {
            if (sub == "get") {
                if (!ok) { errMsg(); return; }
                printf("%s\n", QJsonDocument(data).toJson(QJsonDocument::Indented).constData());
            } else {
                okOrErr();
            }

        // ── audio ────────────────────────────────────────────────────────────
        } else if (cmd == "audio") {
            if (sub == "devices") {
                if (!ok) { errMsg(); return; }
                printf("Input devices:\n");
                for (const auto &v : data["input"].toArray())
                    printf("  %s\n", v.toString().toUtf8().constData());
                printf("Output devices:\n");
                for (const auto &v : data["output"].toArray())
                    printf("  %s\n", v.toString().toUtf8().constData());
            } else {
                okOrErr();
            }

        // ── radio ────────────────────────────────────────────────────────────
        } else if (cmd == "radio") {
            if (sub == "get") {
                if (!ok) { errMsg(); return; }
                static const char *pttNames[] = {"VOX", "CAT", "DTR", "RTS"};
                auto numStr = [&](const char *k) -> std::string {
                    QJsonValue v = data[k];
                    if (v.isDouble()) return std::to_string(v.toDouble());
                    return v.toString("—").toStdString();
                };
                printf("Connected : %s\n", data["connected"].toBool() ? "yes" : "no");
                if (data["connected"].toBool()) {
                    printf("Freq      : %s kHz\n", numStr("freq_khz").c_str());
                    printf("Mode      : %s\n",
                        data["mode"].toString(QStringLiteral("—")).toUtf8().constData());
                }
                printf("Model     : %d\n",  data["rig_model"].toInt());
                printf("Port      : %s\n",
                    data["port"].toString(QStringLiteral("—")).toUtf8().constData());
                printf("Baud      : %d\n",  data["baud"].toInt());
                int ptt = data["ptt_type"].toInt();
                printf("PTT type  : %s\n",
                    (ptt >= 0 && ptt <= 3) ? pttNames[ptt] : "?");
            } else {
                okOrErr();
            }

        // ── messages ─────────────────────────────────────────────────────────
        } else if (cmd == "messages") {
            if (sub == "clear") {
                okOrErr();
            } else {
                if (!ok) { errMsg(); return; }
                QJsonArray msgs  = data["messages"].toArray();
                int total = data["count"].toInt((int)msgs.size());
                printf("Showing %d of %d messages:\n", (int)msgs.size(), total);
                for (const auto &mv : msgs) {
                    QJsonObject m = mv.toObject();
                    QString frm  = m["from"].toString(QStringLiteral("?"));
                    QString to   = m["to"].toString();
                    QString body = m["body"].toString(m["raw"].toString());
                    int     snr  = m["snr_db"].toInt();
                    double  freq = m["freq_hz"].toDouble();
                    QString t    = m["time"].toString();
                    QString dest = to.isEmpty() ? QString() : " → " + to;
                    printf("  [%s] +%.0fHz  SNR%+d  %s%s  %s\n",
                        t.toUtf8().constData(), freq, snr,
                        frm.toUtf8().constData(), dest.toUtf8().constData(),
                        body.toUtf8().constData());
                }
            }

        // ── spectrum ─────────────────────────────────────────────────────────
        } else if (cmd == "spectrum") {
            if (!ok) { errMsg(); return; }
            QJsonArray bins = data["bins"].toArray();
            double hz = data["hz_per_bin"].toDouble();
            printf("%d bins, %.3f Hz/bin\n", (int)bins.size(), hz);
            if (!bins.isEmpty()) {
                int peak = 0;
                for (int j = 1; j < (int)bins.size(); ++j)
                    if (bins[j].toDouble() > bins[peak].toDouble()) peak = j;
                printf("Peak: %.1f dB at %.1f Hz (bin %d)\n",
                    bins[peak].toDouble(), peak * hz, peak);
            }

        // ── send ─────────────────────────────────────────────────────────────
        } else if (cmd == "send") {
            if (!ok) { errMsg(); return; }
            printf("Queued, %d frame(s) now in queue\n", data["queue_size"].toInt());

        // ── tx ───────────────────────────────────────────────────────────────
        } else if (cmd == "tx") {
            if (sub == "queue") {
                if (!ok) { errMsg(); return; }
                QJsonArray q = data["queue"].toArray();
                printf("%d frame(s) in TX queue\n", (int)q.size());
                for (int j = 0; j < (int)q.size(); ++j) {
                    QJsonObject f = q[j].toObject();
                    QString payload = f["payload"].toString();
                    if (payload.length() > 40) payload = payload.left(40);
                    printf("  [%d] submode=%d frame_type=%d payload=%s\n",
                        j, f["submode"].toInt(), f["frame_type"].toInt(),
                        payload.toUtf8().constData());
                }
            } else {
                okOrErr();
            }
        }
    }

    // ── Handle streaming events (monitor / stream) ────────────────────────────

    void handleStreamEvent(const QJsonObject &obj)
    {
        if (obj["type"].toString() != "event") return;
        QString ev   = obj["event"].toString();
        QJsonObject data = obj["data"].toObject();

        if (m_args.cmd == "monitor") {
            // Apply filter
            if (!m_args.monitorFilter.isEmpty() &&
                !ev.toLower().contains(m_args.monitorFilter.toLower()))
                return;

            if (ev == "message.decoded") {
                printDecodedLine(data, /*indent=*/false, /*label=*/nullptr);
            } else if (ev == "tx.started") {
                printf(">>> TX STARTED\n");
            } else if (ev == "tx.finished") {
                printf("<<< TX FINISHED\n");
            } else if (ev == "tx.queued") {
                printf("TX queued: %d frame(s)\n", data["queue_size"].toInt());
            } else if (ev == "radio.connected") {
                auto numStr = [&](const char *k) -> std::string {
                    QJsonValue v = data[k];
                    return v.isDouble() ? std::to_string(v.toDouble()) : "—";
                };
                printf("Radio connected: %s kHz %s\n",
                    numStr("freq_khz").c_str(),
                    data["mode"].toString().toUtf8().constData());
            } else if (ev == "radio.disconnected") {
                printf("Radio disconnected\n");
            } else if (ev == "config.changed") {
                printf("Config changed: %s\n",
                    QJsonDocument(data).toJson(QJsonDocument::Compact).constData());
            } else if (ev == "error") {
                fprintf(stderr, "ERROR: %s\n", data["message"].toString().toUtf8().constData());
            } else if (ev != "status" && ev != "spectrum") {
                // Unknown event — print raw JSON
                printf("%s\n", QJsonDocument(obj).toJson(QJsonDocument::Indented).constData());
            }
            fflush(stdout);

        } else {
            // stream command: only message.decoded (and optionally message.frame)
            if (ev == "message.decoded") {
                outputStreamLine(data, "RX");
            } else if (ev == "message.frame" && m_args.streamFrames) {
                if (m_args.streamJson) {
                    outputLine(QJsonDocument(data).toJson(QJsonDocument::Compact));
                } else {
                    outputLine(fmtFrame(data));
                }
            }
        }
    }

    void outputStreamLine(const QJsonObject &data, const char *label)
    {
        QString line;
        if (m_args.streamJson)
            line = QJsonDocument(data).toJson(QJsonDocument::Compact);
        else
            line = fmtMessage(data, label);
        outputLine(line);
    }

    void outputLine(const QString &line)
    {
        if (m_outStream) {
            *m_outStream << line << "\n";
            m_outStream->flush();
        } else {
            printf("%s\n", line.toUtf8().constData());
            fflush(stdout);
        }
    }

    // ── Formatting helpers ────────────────────────────────────────────────────

    static void printDecodedLine(const QJsonObject &d, bool indent, const char *label)
    {
        QString frm  = d["from"].toString(QStringLiteral("?"));
        QString to   = d["to"].toString();
        QString body = d["body"].toString(d["raw"].toString());
        int     snr  = d["snr_db"].toInt();
        double  freq = d["freq_hz"].toDouble();
        QString t    = d["time"].toString();
        QString dest = to.isEmpty() ? QString() : " → " + to;
        if (label)
            printf("%s[%s] +%.0fHz  SNR%+d  %s  %s%s  %s\n",
                indent ? "  " : "",
                t.toUtf8().constData(), freq, snr, label,
                frm.toUtf8().constData(), dest.toUtf8().constData(),
                body.toUtf8().constData());
        else
            printf("%s[%s] +%.0fHz  SNR%+d  %s%s  %s\n",
                indent ? "  " : "",
                t.toUtf8().constData(), freq, snr,
                frm.toUtf8().constData(), dest.toUtf8().constData(),
                body.toUtf8().constData());
    }

    static QString fmtMessage(const QJsonObject &d, const char *label)
    {
        QString frm  = d["from"].toString(QStringLiteral("?"));
        QString to   = d["to"].toString();
        QString body = d["body"].toString(
                       d["raw"].toString(
                       d["assembled_text"].toString()));
        int     snr  = d["snr_db"].toInt();
        double  freq = d["freq_hz"].toDouble();
        QString t    = d["time"].toString();
        QString dest = to.isEmpty() ? QString() : " → " + to;
        QString snrStr = (snr >= 0 ? QStringLiteral("+") : QString()) + QString::number(snr);
        return QString("[%1] +%2Hz  SNR%3  %4  %5%6  %7")
            .arg(t)
            .arg(freq, 0, 'f', 0)
            .arg(snrStr)
            .arg(QLatin1String(label))
            .arg(frm, dest, body);
    }

    static QString fmtFrame(const QJsonObject &d)
    {
        double  freq  = d["freq_hz"].toDouble();
        int     snr   = d["snr_db"].toInt();
        QString t     = d["time"].toString();
        int     ftype = d["frame_type"].toInt();
        QString text  = d["assembled_text"].toString();
        static const char *ftypeNames[] = {"MID", "FIRST", "LAST", "SINGLE"};
        QString ftypeName = (ftype >= 0 && ftype <= 3)
            ? QLatin1String(ftypeNames[ftype]) : QString::number(ftype);
        QString snrStr = (snr >= 0 ? QStringLiteral("+") : QString()) + QString::number(snr);
        return QString("[%1] +%2Hz  SNR%3  [%4]  %5")
            .arg(t)
            .arg(freq, 0, 'f', 0)
            .arg(snrStr, ftypeName, text);
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────

    void emitFinished(int code)
    {
        if (m_doneCalled) return;
        m_doneCalled = true;
        if (m_timer) m_timer->stop();
        // Abort (not close) to avoid triggering Qt's internal close-handshake
        // timer during app teardown, which produces spurious warnings.
        m_ws->abort();
        QCoreApplication::exit(code);
    }

    // ── Members ───────────────────────────────────────────────────────────────

    Args         m_args;
    QWebSocket  *m_ws         = nullptr;
    QTimer      *m_timer      = nullptr;
    QString      m_cmdId;
    int          m_drainCount = 0;
    bool         m_streaming  = false;
    bool         m_doneCalled = false;
    int          m_exitCode   = 0;
    QFile       *m_outFile    = nullptr;
    QTextStream *m_outStream  = nullptr;
};

// Moc for the inline QObject class (required with CMAKE_AUTOMOC for .cpp files)
#include "jf8-tool.moc"

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("jf8-tool"));
    app.setApplicationVersion(QStringLiteral(JF8CALL_VERSION_STR));

    Args args;
    int parseExit = 0;
    if (!parseArgs(argc, argv, args, parseExit))
        return parseExit;

    // Ctrl-C for streaming commands
    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    auto *tool = new Jf8Tool(std::move(args), &app);
    QTimer::singleShot(0, tool, &Jf8Tool::start);
    return app.exec();
}
