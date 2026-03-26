// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "wsserver.h"
#include "messagemodel.h"
#include "waterfallwidget.h"
#include "audioinput.h"
#include "audiooutput.h"
#include <QScrollArea>
#include "periodclock.h"
#include "js8message.h"
#include "DecodedText.h"
#include "pskreporter.h"

#include <QApplication>
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QTableWidget>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QLabel>
#include <QThread>
#include <QMetaObject>
#include <QDateTime>
#include <QCloseEvent>
#include <QSet>
#include <QTextCursor>
#include <QTextBlock>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QGroupBox>
#include <cmath>
#include <algorithm>

#include "gfsk8_modem.h"
#ifdef HAVE_CODEC2
#include "codec2_modem.h"
#endif
#ifdef HAVE_OLIVIA
#include "olivia_modem.h"
#endif
#ifdef HAVE_PSK_MODEM
#include "psk_modem.h"
#endif
#include "jf8call_version.h"
#include "buildinfo.h"
#include "updatechecker.h"
#include "checksum.h"
#include "gridcache.h"
#include "messageinbox.h"

// ── Decode worker ─────────────────────────────────────────────────────────────
// Runs IModem::feedAudio in a background thread; emits results as QVariantMaps.

class DecodeWorker : public QObject {
    Q_OBJECT
public:
    explicit DecodeWorker(int modemType, int submode = 0, QObject *parent = nullptr)
        : QObject(parent)
    {
#ifdef HAVE_CODEC2
        if (modemType == 1) {
            m_modem = std::make_unique<Codec2Modem>(submode);
        } else
#endif
#ifdef HAVE_OLIVIA
        if (modemType == 2) {
            m_modem = std::make_unique<OliviaModem>(submode);
        } else
#endif
#ifdef HAVE_PSK_MODEM
        if (modemType == 3) {
            m_modem = std::make_unique<PskModem>(submode);
        } else
#endif
        {
            m_modem = std::make_unique<Gfsk8Modem>();
            Q_UNUSED(modemType)
            Q_UNUSED(submode)
        }
    }

public slots:
    void decode(QByteArray samples, int utc, int /*submodes*/)
    {
        const int16_t *ptr = reinterpret_cast<const int16_t *>(samples.constData());
        size_t count = static_cast<size_t>(samples.size() / sizeof(int16_t));

        QList<QVariantMap> results;
        m_modem->feedAudio(
            std::span<const int16_t>(ptr, count),
            utc,
            [&](const ModemDecoded &d) {
                QVariantMap m;
                m[QStringLiteral("modemType")] = d.modemType;
                m[QStringLiteral("isRawText")] = d.isRawText;
                m[QStringLiteral("message")]   = QString::fromStdString(d.message);
                if (d.isRawText) {
                    m[QStringLiteral("rawText")] = QString::fromStdString(d.message);
                } else {
                    DecodedText dt(d.message, d.frameType, d.submode);
                    m[QStringLiteral("rawText")] = QString::fromStdString(dt.message());
                }
                m[QStringLiteral("snrDb")]     = d.snrDb;
                m[QStringLiteral("freqHz")]    = d.frequencyHz;
                m[QStringLiteral("submode")]   = d.submode;
                m[QStringLiteral("frameType")] = d.frameType;
                m[QStringLiteral("isSyncMark")] = d.isSyncMark;
                m[QStringLiteral("isEom")]       = d.isEom;
                results.append(m);
            });
        emit decodeFinished(results);
    }

signals:
    void decodeFinished(QList<QVariantMap> results);

private:
    std::unique_ptr<IModem> m_modem;
};

// ── MainWindow ────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_model(new MessageModel(this))
    , m_audioIn(new AudioInput(this))
    , m_audioOut(new AudioOutput(this))
    , m_periodClock(new PeriodClock(this))
    , m_hamlib(new HamlibController)
    , m_modem(std::make_unique<Gfsk8Modem>())
{
    m_config = Config::load();

    // Restore configured modem type
#ifdef HAVE_CODEC2
    if (m_config.modemType == 1)
        m_modem = std::make_unique<Codec2Modem>(m_config.submode);
    else
#endif
#ifdef HAVE_OLIVIA
    if (m_config.modemType == 2)
        m_modem = std::make_unique<OliviaModem>(m_config.submode);
    else
#endif
#ifdef HAVE_PSK_MODEM
    if (m_config.modemType == 3)
        m_modem = std::make_unique<PskModem>(m_config.submode);
    else
#endif
        m_modem = std::make_unique<Gfsk8Modem>();

    setupUi();
    applyStyleSheet();

    // Hamlib in its own thread
    m_hamlibThread = new QThread(this);
    m_hamlib->moveToThread(m_hamlibThread);
    connect(m_hamlibThread, &QThread::finished, m_hamlib, &QObject::deleteLater);
    connect(m_hamlib, &HamlibController::connectionChanged,
            this, &MainWindow::onRadioConnectionChanged);
    connect(m_hamlib, &HamlibController::pollResult,
            this, &MainWindow::onRadioPollResult);
    connect(m_hamlib, &HamlibController::error,
            this, &MainWindow::onRadioError);
    m_hamlibThread->start();

    // Decode worker in its own thread
    restartDecodeWorker();

    // Streaming TX timer (fires when a streaming modem has queued TX frames)
    m_streamTxTimer = new QTimer(this);
    m_streamTxTimer->setInterval(500);
    m_streamTxTimer->setSingleShot(false);
    connect(m_streamTxTimer, &QTimer::timeout, this, &MainWindow::onStreamTxTimer);

    // Period clock
    connect(m_periodClock, &PeriodClock::periodStarted,
            this, &MainWindow::onPeriodStarted);

    const bool isPeriodic = (m_modem->submodeParms(0).periodSeconds > 0);
    if (isPeriodic) {
        m_periodClock->setPeriodSeconds(m_modem->submodeParms(m_config.submode).periodSeconds);
        m_periodClock->start();
    } else {
        m_audioChunkConn = connect(
            m_audioIn, &AudioInput::audioChunkReady,
            this, &MainWindow::onAudioChunkReady, Qt::QueuedConnection);
        m_streamTxTimer->start();
    }

    // Audio input spectrum → waterfall
    connect(m_audioIn, &AudioInput::spectrumReady,
            this, &MainWindow::onSpectrumReady, Qt::QueuedConnection);
    connect(m_audioIn, &AudioInput::error,
            this, [this](const QString &msg) {
                statusBar()->showMessage(QStringLiteral("Audio error: ") + msg, 5000);
            });

    // Audio output
    connect(m_audioOut, &AudioOutput::playbackFinished,
            this, &MainWindow::onPlaybackFinished, Qt::QueuedConnection);
    connect(m_audioOut, &AudioOutput::error,
            this, [this](const QString &msg) {
                statusBar()->showMessage(QStringLiteral("Audio output error: ") + msg, 5000);
            });

    // Radio poll timer (2 Hz while connected)
    m_radioPollTimer = new QTimer(this);
    m_radioPollTimer->setInterval(500);
    connect(m_radioPollTimer, &QTimer::timeout,
            this, &MainWindow::onRadioPollTimer);

    // Auto-reconnect: retry every 15 s when a port is configured but not connected
    m_radioReconnectTimer = new QTimer(this);
    m_radioReconnectTimer->setInterval(15000);
    connect(m_radioReconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_config.rigPort.isEmpty() && !m_hamlib->isConnected()) {
            const RigConfig cfg = configToRigConfig();
            QMetaObject::invokeMethod(m_hamlib, "connectRig", Qt::QueuedConnection,
                Q_ARG(RigConfig, cfg));
        }
    });
    m_radioReconnectTimer->start();

    // Heartbeat timer — check every period
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(1000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &MainWindow::onHeartbeatCheck);
    m_heartbeatTimer->start();

    startAudio();

    // WebSocket API server
    if (m_config.wsEnabled) {
        m_wsServer = new WsServer(this, this);
        if (!m_wsServer->listen(static_cast<quint16>(m_config.wsPort), m_config.wsHost)) {
            statusBar()->showMessage(
                QStringLiteral("WS API: failed to listen on port %1").arg(m_config.wsPort), 5000);
            delete m_wsServer;
            m_wsServer = nullptr;
        }
    }

    // Push status to WS clients at 1 Hz
    m_wsStatusTimer = new QTimer(this);
    m_wsStatusTimer->setInterval(1000);
    connect(m_wsStatusTimer, &QTimer::timeout, this, [this]() {
        if (m_wsServer) m_wsServer->pushStatus();
    });
    m_wsStatusTimer->start();

    // PSK Reporter
    m_pskReporter = new PskReporter(this);

    // Frame cleanup timer: checks for timed-out GFSK8 multi-frame buffers
    m_frameCleanupTimer = new QTimer(this);
    m_frameCleanupTimer->setInterval(15000); // check every 15 seconds
    connect(m_frameCleanupTimer, &QTimer::timeout, this, &MainWindow::onFrameCleanupTimer);
    m_frameCleanupTimer->start();

    if (m_config.pskReporterEnabled) {
        m_pskReporter->setLocalStation(m_config.callsign, m_config.grid,
                                        QStringLiteral("JF8Call/%1").arg(
                                            QString::fromLatin1(JF8CALL_VERSION_STR)));
    }

    // Update checker — runs async; shows update bar if a newer build exists
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable,
            this, [this]() { if (m_updateBar) m_updateBar->setVisible(true); });
    m_updateChecker->checkForUpdates();

    // Defer expiry / startup checks until the event loop is running so
    // any modal dialogs are displayed correctly.
    QTimer::singleShot(0, this, &MainWindow::startupChecks);
}

MainWindow::~MainWindow()
{
    if (m_radioReconnectTimer) m_radioReconnectTimer->stop();
    stopAudio();
    if (m_hamlibThread) {
        QMetaObject::invokeMethod(m_hamlib, &HamlibController::disconnectRig,
                                  Qt::BlockingQueuedConnection);
        m_hamlibThread->quit();
        m_hamlibThread->wait();
    }
    if (m_decodeThread) {
        m_decodeThread->quit();
        m_decodeThread->wait();
    }
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    if (m_splitterRestored) return;
    m_splitterRestored = true;

    // Restore window geometry first so the splitter has the right total size
    if (!m_config.windowGeometry.isEmpty())
        restoreGeometry(m_config.windowGeometry);
    if (!m_config.windowState.isEmpty())
        restoreState(m_config.windowState);

    // Then restore splitter positions (requires the widget to be shown/sized)
    if (m_vSplit && !m_config.vSplitterState.isEmpty())
        m_vSplit->restoreState(m_config.vSplitterState);
    if (m_hSplit && !m_config.hSplitterState.isEmpty())
        m_hSplit->restoreState(m_config.hSplitterState);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    m_config.windowGeometry = saveGeometry();
    m_config.windowState    = saveState();
    if (m_vSplit) m_config.vSplitterState = m_vSplit->saveState();
    if (m_hSplit) m_config.hSplitterState = m_hSplit->saveState();
    m_config.save();
    e->accept();
}

void MainWindow::startupChecks()
{
    // ALPHA / BETA expiry: builds tagged with either keyword stop working 30 days
    // after their compile timestamp so stale pre-release builds do not persist.
    const QLatin1String ver(JF8CALL_VERSION_STR);
    const bool isPreRelease =
        ver.contains(QLatin1String("ALPHA")) ||
        ver.contains(QLatin1String("BETA"));

    if (isPreRelease) {
        constexpr long long k_expirySeconds = 30LL * 86400;
        const long long now = QDateTime::currentSecsSinceEpoch();
        if (now > g_buildTime + k_expirySeconds) {
            const long long daysOver = (now - g_buildTime) / 86400 - 30;
            const QString label = ver.contains(QLatin1String("ALPHA"))
                                      ? QStringLiteral("ALPHA")
                                      : QStringLiteral("BETA");
            QMessageBox::warning(this,
                QStringLiteral("Build Expired"),
                QStringLiteral(
                    "<p>This %1 build of JF8Call expired %2 day(s) ago.</p>"
                    "<p>Please download the latest version from:<br>"
                    "<a href='https://ordo-artificum.com/products/jf8call/'>"
                    "ordo-artificum.com/products/jf8call/</a></p>")
                .arg(label, QString::number(daysOver)));
            qApp->quit();
        }
    }
}

// ── UI Setup ─────────────────────────────────────────────────────────────────

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("JF8Call %1").arg(JF8CALL_VERSION_STR));
    setMinimumSize(900, 600);
    resize(1200, 750);

    setupMenuBar();
    setupToolBar();
    setupCentralWidget();
    setupStatusBar();
}

// Enumerate serial ports present on this system.
// USB/ACM adapters included when plugged in; ttyS* only when sysfs shows real hardware.
// Network rigctld endpoints appended at the end; combo is editable for manual entry.
static QStringList detectSerialPorts()
{
    QStringList ports;
#if defined(Q_OS_WIN)
    QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM"),
                  QSettings::NativeFormat);
    for (const QString &key : reg.allKeys())
        ports << reg.value(key).toString();
    ports.sort();
#else
    QDir dev(QStringLiteral("/dev"));
    for (const QString &name : dev.entryList(
             {QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*")}, QDir::System))
        ports << QStringLiteral("/dev/") + name;
    for (const QString &name : dev.entryList({QStringLiteral("ttyS*")}, QDir::System)) {
        if (QFileInfo::exists(QStringLiteral("/sys/class/tty/%1/device").arg(name)))
            ports << QStringLiteral("/dev/") + name;
    }
    ports.sort();
#endif
    ports << QStringLiteral("localhost:4532");
    ports << QStringLiteral("localhost:4533");
    return ports;
}

void MainWindow::setupMenuBar()
{
    auto *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Preferences…"), this, &MainWindow::onPreferences);
    file->addSeparator();
    file->addAction(tr("&Quit"), qApp, &QApplication::quit);

    auto *radio = menuBar()->addMenu(tr("&Radio"));
    radio->addAction(tr("Radio &Setup…"), this, &MainWindow::onRadioSetup);
    radio->addAction(tr("&Disconnect Radio"), this, [this]() {
        QMetaObject::invokeMethod(m_hamlib, &HamlibController::disconnectRig,
                                  Qt::QueuedConnection);
    });

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About JF8Call…"), this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("About JF8Call"));
        dlg.setMinimumWidth(500);

        auto *vbox = new QVBoxLayout(&dlg);
        vbox->setSpacing(12);
        vbox->setContentsMargins(20, 20, 20, 20);

        auto *nameLbl = new QLabel(QStringLiteral("JF8Call"), &dlg);
        QFont nameFont = nameLbl->font();
        nameFont.setPointSize(22);
        nameFont.setBold(true);
        nameLbl->setFont(nameFont);
        nameLbl->setAlignment(Qt::AlignCenter);
        nameLbl->setStyleSheet(QStringLiteral("color: #c9a84c;"));
        vbox->addWidget(nameLbl);

        const QString buildDate = QStringLiteral(__DATE__);
        const QString releaseTime = QDateTime::fromSecsSinceEpoch(g_buildTime)
                                        .toUTC()
                                        .toString(QStringLiteral("yyyy-MM-dd HH:mm UTC"));
        auto *infoLbl = new QLabel(
            QStringLiteral(
                "<p style='text-align:center;'>"
                "Version " JF8CALL_VERSION_STR "<br>"
                "Built: %1<br>"
                "Release: %2<br><br>"
                "JS8Call-compatible digital mode application<br>"
                "Copyright 2026 Ordo Artificum LLC<br>"
                "Jeff Francis, N0GQ"
                "</p>").arg(buildDate, releaseTime), &dlg);
        infoLbl->setAlignment(Qt::AlignCenter);
        infoLbl->setWordWrap(true);
        vbox->addWidget(infoLbl);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        vbox->addWidget(buttons);

        dlg.exec();
    });
}

void MainWindow::setupToolBar()
{
    auto *tb = addToolBar(QStringLiteral("Main"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setMovable(false);

    // Callsign / grid display (click Preferences to edit)
    m_callGridLabel = new QLabel;
    m_callGridLabel->setObjectName(QStringLiteral("callGridLabel"));
    m_callGridLabel->setMinimumWidth(130);
    {
        const QString cs = m_config.callsign.isEmpty() ? tr("(no call)") : m_config.callsign;
        const QString gr = m_config.grid.isEmpty()     ? tr("?")         : m_config.grid;
        m_callGridLabel->setText(QStringLiteral(" %1 / %2 ").arg(cs, gr));
    }
    m_callGridLabel->setToolTip(tr("Set callsign and grid in Preferences"));
    tb->addWidget(m_callGridLabel);

    tb->addSeparator();

    auto *freqLabel = new QLabel(tr(" Freq (kHz):"));
    freqLabel->setObjectName(QStringLiteral("toolbarLabel"));
    tb->addWidget(freqLabel);

    m_freqSpin = new QDoubleSpinBox;
    m_freqSpin->setRange(1800.0, 30000.0);
    m_freqSpin->setDecimals(3);
    m_freqSpin->setSingleStep(0.5);
    m_freqSpin->setValue(m_config.frequencyKhz);
    m_freqSpin->setMaximumWidth(110);
    connect(m_freqSpin, &QDoubleSpinBox::valueChanged,
            this, [this](double v) {
                m_config.frequencyKhz = v;
                m_config.save();
                if (m_wsServer) m_wsServer->pushConfigChanged();
            });
    tb->addWidget(m_freqSpin);

    // Standard JS8Call band frequencies dropdown
    auto *freqPreset = new QComboBox;
    freqPreset->setObjectName(QStringLiteral("freqPreset"));
    freqPreset->setMaximumWidth(80);
    freqPreset->setToolTip(tr("Select a standard JS8Call frequency"));
    freqPreset->addItem(tr("Band"), 0.0);          // placeholder
    for (auto [label, khz] : std::initializer_list<std::pair<const char*, double>>{
            {"160m", 1840.0}, {"80m",  3578.0}, {"60m",  5357.0},
            {"40m",  7078.0}, {"30m", 10130.0}, {"20m", 14078.0},
            {"17m", 18104.0}, {"15m", 21078.0}, {"12m", 24922.0},
            {"10m", 28078.0}, {"6m",  50318.0}})
        freqPreset->addItem(QString::fromLatin1(label), khz);
    connect(freqPreset, QOverload<int>::of(&QComboBox::activated),
            this, [this, freqPreset](int idx) {
                if (idx == 0) return;
                const double khz = freqPreset->itemData(idx).toDouble();
                freqPreset->setCurrentIndex(0);  // reset to placeholder
                m_freqSpin->setValue(khz);        // saves config via valueChanged
                apiSetFrequency(khz);             // tune the radio if connected
                if (m_config.autoAtu && m_hamlib->isConnected())
                    QMetaObject::invokeMethod(m_hamlib, &HamlibController::startTune,
                                              Qt::QueuedConnection);
            });
    tb->addWidget(freqPreset);

    auto *modeLabel = new QLabel(tr(" Mode:"));
    modeLabel->setObjectName(QStringLiteral("toolbarLabel"));
    tb->addWidget(modeLabel);

    m_submodeCbo = new QComboBox;
    refreshSubmodeCombo();
    connect(m_submodeCbo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                m_config.submode = idx;
                m_config.save();
                const int period = m_modem->submodeParms(idx).periodSeconds;
                if (period > 0)
                    m_periodClock->setPeriodSeconds(period);
                if (m_wsServer) m_wsServer->pushConfigChanged();
            });
    tb->addWidget(m_submodeCbo);

    tb->addSeparator();

    auto *txLabel = new QLabel(tr(" TX Hz:"));
    txLabel->setObjectName(QStringLiteral("toolbarLabel"));
    tb->addWidget(txLabel);

    m_txFreqSpin = new QDoubleSpinBox;
    m_txFreqSpin->setRange(200.0, 3900.0);
    m_txFreqSpin->setDecimals(0);
    m_txFreqSpin->setValue(m_config.txFreqHz);
    m_txFreqSpin->setMaximumWidth(75);
    connect(m_txFreqSpin, &QDoubleSpinBox::valueChanged,
            this, [this](double v) {
                m_config.txFreqHz = v;
                m_config.save();
                if (m_waterfall) m_waterfall->setTxFreqHz(static_cast<float>(v));
                if (m_wsServer)  m_wsServer->pushConfigChanged();
            });
    tb->addWidget(m_txFreqSpin);

    tb->addSeparator();

    m_hbCheck = new QCheckBox(tr("HB"));
    m_hbCheck->setChecked(m_config.heartbeatEnabled);
    connect(m_hbCheck, &QCheckBox::toggled, this, [this](bool v) {
        m_config.heartbeatEnabled = v;
        m_config.save();
        if (m_wsServer) m_wsServer->pushConfigChanged();
    });
    tb->addWidget(m_hbCheck);

    tb->addSeparator();

    m_connectBtn = new QPushButton(tr("[C]onnect Radio"));
    m_connectBtn->setObjectName(QStringLiteral("connectBtn"));
    connect(m_connectBtn, &QPushButton::clicked,
            this, &MainWindow::onRadioSetup);
    tb->addWidget(m_connectBtn);
}

void MainWindow::setupCentralWidget()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(0);

    // ── Outer vertical splitter: top panels / interactive / waterfall ─────
    m_vSplit = new QSplitter(Qt::Vertical);
    auto *vSplit = m_vSplit;

    // ── Top: Heard text log (left) | Info table (right) ──────────────────
    m_hSplit = new QSplitter(Qt::Horizontal);

    // Heard pane — running plain-text log of all decoded messages
    auto *heardWidget = new QWidget;
    auto *heardLayout = new QVBoxLayout(heardWidget);
    heardLayout->setContentsMargins(2, 2, 2, 2);
    heardLayout->setSpacing(2);
    auto *heardLabel = new QLabel(tr("Heard"));
    heardLabel->setObjectName(QStringLiteral("paneLabel"));
    heardLayout->addWidget(heardLabel);
    m_infoPane = new QPlainTextEdit;     // "Heard" text log
    m_infoPane->setReadOnly(true);
    m_infoPane->setObjectName(QStringLiteral("infoPane"));
    heardLayout->addWidget(m_infoPane, 1);
    m_hSplit->addWidget(heardWidget);

    // Info pane — per-message detail table with grid/dist/bearing
    auto *infoWidget = new QWidget;
    auto *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(2, 2, 2, 2);
    infoLayout->setSpacing(2);
    auto *infoLabel = new QLabel(tr("Info"));
    infoLabel->setObjectName(QStringLiteral("paneLabel"));
    infoLayout->addWidget(infoLabel);
    m_messageTable = new QTableView;
    m_messageTable->setModel(m_model);
    m_messageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_messageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_messageTable->setAlternatingRowColors(false);
    m_messageTable->verticalHeader()->hide();
    m_messageTable->horizontalHeader()->setStretchLastSection(true);
    m_messageTable->setSortingEnabled(true);
    m_messageTable->sortByColumn(MessageModel::ColAge, Qt::AscendingOrder);
    m_messageTable->setColumnWidth(MessageModel::ColAge,     42);   // Age
    m_messageTable->setColumnWidth(MessageModel::ColTime,    62);   // Time
    m_messageTable->setColumnWidth(MessageModel::ColFreq,    50);   // Freq
    m_messageTable->setColumnWidth(MessageModel::ColSnr,     38);   // SNR
    m_messageTable->setColumnWidth(MessageModel::ColSubmode, 48);   // Mode
    m_messageTable->setColumnWidth(MessageModel::ColFrom,    70);   // From
    m_messageTable->setColumnWidth(MessageModel::ColMessage, 140);  // Message
    m_messageTable->setColumnWidth(MessageModel::ColGrid,    55);   // Grid
    m_messageTable->setColumnWidth(MessageModel::ColDist,    60);   // Dist
    // ColBearing gets stretch
    connect(m_messageTable, &QTableView::clicked,
            this, &MainWindow::onInfoTableClicked);
    infoLayout->addWidget(m_messageTable, 1);
    m_hSplit->addWidget(infoWidget);

    m_hSplit->setStretchFactor(0, 1);
    m_hSplit->setStretchFactor(1, 1);
    vSplit->addWidget(m_hSplit);

    // ── Interactive pane (QSO at current offset) ──────────────────────────
    auto *interWidget = new QWidget;
    auto *interLayout = new QVBoxLayout(interWidget);
    interLayout->setContentsMargins(2, 2, 2, 2);
    interLayout->setSpacing(2);
    auto *interLabel = new QLabel(tr("Interactive"));
    interLabel->setObjectName(QStringLiteral("paneLabel"));
    interLayout->addWidget(interLabel);
    m_interactiveDisplay = new QPlainTextEdit;
    m_interactiveDisplay->setReadOnly(true);
    m_interactiveDisplay->setObjectName(QStringLiteral("interactiveDisplay"));
    interLayout->addWidget(m_interactiveDisplay, 1);

    // Input row: quick buttons + text entry + send
    // Order: CQ | HB | SNR? | INFO? | GRID? | STATUS? | HEARING? | Deselect | txEdit… | MSG | Send | HALT
    auto *txRow = new QHBoxLayout;
    m_cqBtn = new QPushButton(tr("CQ"));
    m_cqBtn->setObjectName(QStringLiteral("cqBtn"));
    connect(m_cqBtn, &QPushButton::clicked, this, &MainWindow::onCqClicked);
    txRow->addWidget(m_cqBtn);
    m_hbBtn = new QPushButton(tr("HB"));
    m_hbBtn->setObjectName(QStringLiteral("quickBtn"));
    connect(m_hbBtn, &QPushButton::clicked, this, &MainWindow::onHbClicked);
    txRow->addWidget(m_hbBtn);
    m_snrBtn = new QPushButton(tr("SNR?"));
    m_snrBtn->setObjectName(QStringLiteral("quickBtn"));
    connect(m_snrBtn, &QPushButton::clicked, this, &MainWindow::onSnrQueryClicked);
    txRow->addWidget(m_snrBtn);
    m_infoBtn = new QPushButton(tr("INFO?"));
    m_infoBtn->setObjectName(QStringLiteral("quickBtn"));
    connect(m_infoBtn, &QPushButton::clicked, this, &MainWindow::onInfoQueryClicked);
    txRow->addWidget(m_infoBtn);
    m_gridBtn = new QPushButton(tr("GRID?"));
    m_gridBtn->setObjectName(QStringLiteral("quickBtn"));
    connect(m_gridBtn, &QPushButton::clicked, this, &MainWindow::onGridQueryClicked);
    txRow->addWidget(m_gridBtn);
    m_statusBtn = new QPushButton(tr("STATUS?"));
    m_statusBtn->setObjectName(QStringLiteral("quickBtn"));
    connect(m_statusBtn, &QPushButton::clicked, this, &MainWindow::onStatusQueryClicked);
    txRow->addWidget(m_statusBtn);
    m_hearingBtn = new QPushButton(tr("HEARING?"));
    m_hearingBtn->setObjectName(QStringLiteral("quickBtn"));
    connect(m_hearingBtn, &QPushButton::clicked, this, &MainWindow::onHearingQueryClicked);
    txRow->addWidget(m_hearingBtn);
    m_deselectBtn = new QPushButton(tr("Deselect"));
    m_deselectBtn->setObjectName(QStringLiteral("deselectBtn"));
    m_deselectBtn->setEnabled(false);
    connect(m_deselectBtn, &QPushButton::clicked, this, &MainWindow::onDeselectClicked);
    txRow->addWidget(m_deselectBtn);
    m_txEdit = new QLineEdit;
    m_txEdit->setPlaceholderText(tr("TX on offset Hz — Enter to send"));
    connect(m_txEdit, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    txRow->addWidget(m_txEdit, 1);
    m_msgBtn = new QPushButton(tr("MSG"));
    m_msgBtn->setObjectName(QStringLiteral("msgBtn"));
    m_msgBtn->setEnabled(false);
    connect(m_msgBtn, &QPushButton::clicked, this, &MainWindow::onMsgBtnClicked);
    txRow->addWidget(m_msgBtn);
    m_sendBtn = new QPushButton(tr("Send"));
    m_sendBtn->setObjectName(QStringLiteral("sendBtn"));
    connect(m_sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    txRow->addWidget(m_sendBtn);
    m_haltBtn = new QPushButton(tr("HALT"));
    m_haltBtn->setObjectName(QStringLiteral("haltBtn"));
    connect(m_haltBtn, &QPushButton::clicked, this, &MainWindow::onHaltClicked);
    txRow->addWidget(m_haltBtn);
    interLayout->addLayout(txRow);
    vSplit->addWidget(interWidget);

    // ── Waterfall (bottom) ────────────────────────────────────────────────
    m_waterfall = new WaterfallWidget;
    m_waterfall->setTxFreqHz(static_cast<float>(m_config.txFreqHz));
    connect(m_waterfall, &WaterfallWidget::frequencyClicked,
            this, &MainWindow::onWaterfallFreqClicked);
    vSplit->addWidget(m_waterfall);

    vSplit->setSizes({600, 200, 200});   // default; overridden in showEvent

    // ── Update notification bar (hidden until a newer version is detected) ──
    m_updateBar = new QFrame(central);
    m_updateBar->setObjectName(QStringLiteral("updateBar"));
    m_updateBar->setVisible(false);
    auto *barLayout = new QHBoxLayout(m_updateBar);
    barLayout->setContentsMargins(12, 6, 8, 6);
    barLayout->setSpacing(8);

    auto *updateIcon = new QLabel(QStringLiteral("\u2605"), m_updateBar);  // ★
    updateIcon->setObjectName(QStringLiteral("updateBarIcon"));

    auto *updateLbl = new QLabel(m_updateBar);
    updateLbl->setObjectName(QStringLiteral("updateBarLabel"));
    updateLbl->setTextFormat(Qt::RichText);
    updateLbl->setText(
        QStringLiteral("A newer version of JF8Call is available. &nbsp;"
                       "<a href='https://ordo-artificum.com/products/jf8call/'>"
                       "Visit ordo-artificum.com/products/jf8call/ to download.</a>"));
    updateLbl->setOpenExternalLinks(true);

    auto *dismissBtn = new QPushButton(QStringLiteral("\u00d7"), m_updateBar);  // ×
    dismissBtn->setObjectName(QStringLiteral("updateBarDismiss"));
    dismissBtn->setFixedSize(22, 22);
    dismissBtn->setFlat(true);
    dismissBtn->setToolTip(QStringLiteral("Dismiss"));
    connect(dismissBtn, &QPushButton::clicked, m_updateBar, &QFrame::hide);

    barLayout->addWidget(updateIcon);
    barLayout->addWidget(updateLbl, 1);
    barLayout->addWidget(dismissBtn);

    mainLayout->addWidget(m_updateBar);
    mainLayout->addWidget(vSplit, 1);
}

void MainWindow::setupStatusBar()
{
    m_radioStatusLabel = new QLabel(tr("Radio: not connected"));
    m_radioStatusLabel->setObjectName(QStringLiteral("radioStatusLabel"));
    statusBar()->addPermanentWidget(m_radioStatusLabel);

    m_rxTxLabel = new QLabel(tr("RX"));
    m_rxTxLabel->setObjectName(QStringLiteral("rxTxLabel"));
    statusBar()->addPermanentWidget(m_rxTxLabel);

    m_countLabel = new QLabel(tr("0 decoded"));
    m_countLabel->setObjectName(QStringLiteral("countLabel"));
    statusBar()->addPermanentWidget(m_countLabel);

    m_inboxNotifyBtn = new QPushButton(tr("Inbox"));
    m_inboxNotifyBtn->setObjectName(QStringLiteral("inboxNotifyBtn"));
    m_inboxNotifyBtn->hide();
    connect(m_inboxNotifyBtn, &QPushButton::clicked, this, &MainWindow::onOpenInbox);
    statusBar()->addPermanentWidget(m_inboxNotifyBtn);
}

// ── Style ─────────────────────────────────────────────────────────────────────

void MainWindow::applyStyleSheet()
{
    setStyleSheet(QStringLiteral(R"(
QMainWindow, QWidget {
    background-color: #1a1a2e;
    color: #e8e0d0;
    font-family: "Inter", "Segoe UI", sans-serif;
    font-size: 12px;
}
QToolBar {
    background-color: #16213e;
    border-bottom: 1px solid #333355;
    spacing: 4px;
    padding: 2px 4px;
}
QLabel#toolbarLabel {
    color: #c9a84c;
    font-weight: bold;
}
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox {
    background-color: #0d1020;
    border: 1px solid #333355;
    border-radius: 3px;
    color: #e8e0d0;
    padding: 2px 4px;
}
QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus {
    border-color: #c9a84c;
}
QPushButton {
    background-color: #16213e;
    border: 1px solid #333355;
    border-radius: 3px;
    color: #e8e0d0;
    padding: 4px 10px;
}
QPushButton:hover { background-color: #1e2d50; }
QPushButton:pressed { background-color: #0d1020; }
QPushButton#sendBtn {
    background-color: #1a3a1a;
    color: #7fbf7f;
    border-color: #2a5a2a;
    font-weight: bold;
}
QPushButton#connectBtn {
    background-color: #1a3a1a;
    color: #c9a84c;
    border-color: #c9a84c;
    font-weight: bold;
}
QPushButton#quickBtn {
    background-color: #0d1020;
    color: #c9a84c;
    border-color: #555;
    padding: 2px 8px;
}
QPushButton#cqBtn {
    background-color: #1a1a3a;
    color: #7fbfff;
    border-color: #3355aa;
    padding: 2px 8px;
    font-weight: bold;
}
QPushButton#haltBtn {
    background-color: #3a1a1a;
    color: #ff6060;
    border-color: #aa3333;
    padding: 2px 8px;
    font-weight: bold;
}
QPushButton#deselectBtn {
    background-color: #1a1a1a;
    color: #888899;
    border-color: #444;
    padding: 2px 8px;
}
QPushButton#msgBtn {
    background-color: #0d1020;
    color: #c9a84c;
    border-color: #555;
    padding: 2px 8px;
    font-weight: bold;
}
QPushButton#inboxNotifyBtn {
    background-color: #1a2a3a;
    color: #7fbfff;
    border-color: #3355aa;
    padding: 2px 8px;
    font-weight: bold;
}
QCheckBox { color: #c9a84c; }
QTableView {
    background-color: #0d1020;
    alternate-background-color: #11152a;
    border: 1px solid #333355;
    gridline-color: #222244;
    selection-background-color: #8b6914;
    selection-color: #fff;
}
QHeaderView::section {
    background-color: #16213e;
    color: #c9a84c;
    border: 1px solid #333355;
    padding: 2px 4px;
    font-weight: bold;
}
QStatusBar {
    background-color: #16213e;
    color: #c9a84c;
    border-top: 1px solid #333355;
}
QLabel#rxTxLabel { color: #7fbf7f; font-weight: bold; }
QLabel#paneLabel {
    color: #c9a84c;
    font-weight: bold;
    font-size: 11px;
    padding: 1px 4px;
    background-color: #16213e;
    border-bottom: 1px solid #333355;
}
QPlainTextEdit#infoPane, QPlainTextEdit#interactiveDisplay {
    background-color: #0d1020;
    color: #e8e0d0;
    border: 1px solid #333355;
    font-family: "Courier New", "Courier", monospace;
    font-size: 12px;
}
QMenuBar {
    background-color: #16213e;
    color: #e8e0d0;
}
QMenuBar::item:selected { background-color: #333355; }
QMenu {
    background-color: #1a1a2e;
    color: #e8e0d0;
    border: 1px solid #333355;
}
QMenu::item:selected { background-color: #333355; }
QScrollBar:vertical {
    background: #0d1020;
    width: 10px;
}
QScrollBar::handle:vertical {
    background: #333355;
    border-radius: 4px;
}
    )"));
}

// ── Audio ─────────────────────────────────────────────────────────────────────

void MainWindow::startAudio()
{
    if (m_audioStarted) return;
    const int rate = m_modem->sampleRate();
    m_audioIn->setTargetRate(rate);
    const bool streaming = (m_modem->submodeParms(0).periodSeconds == 0);
    m_audioIn->setChunkingEnabled(streaming, 100);
    if (!m_audioIn->start(m_config.audioInputName)) return;
    m_audioOut->open(m_config.audioOutputName);
    m_audioStarted = true;
}

void MainWindow::stopAudio()
{
    m_audioIn->stop();
    m_audioOut->close();
    m_audioStarted = false;
}

void MainWindow::restartDecodeWorker()
{
    if (m_decodeThread) {
        disconnect(this, &MainWindow::requestDecode, nullptr, nullptr);
        m_decodeThread->quit();
        m_decodeThread->wait();
        delete m_decodeThread;
        m_decodeThread = nullptr;
    }
    m_decodeThread = new QThread(this);
    auto *worker = new DecodeWorker(m_config.modemType, m_config.submode);
    worker->moveToThread(m_decodeThread);
    connect(m_decodeThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &MainWindow::requestDecode, worker, &DecodeWorker::decode, Qt::QueuedConnection);
    connect(worker, &DecodeWorker::decodeFinished, this, &MainWindow::onDecodeFinished, Qt::QueuedConnection);
    m_decodeThread->start();
}

void MainWindow::refreshSubmodeCombo()
{
    if (!m_submodeCbo) return;
    QSignalBlocker b(m_submodeCbo);
    m_submodeCbo->clear();
    const int n = m_modem->submodeCount();
    for (int i = 0; i < n; ++i) {
        const auto &p = m_modem->submodeParms(i);
        QString label = QString::fromStdString(p.name);
        if (p.periodSeconds > 0)
            label += QStringLiteral(" (%1s)").arg(p.periodSeconds);
        m_submodeCbo->addItem(label);
    }
    const int cur = std::clamp(m_config.submode, 0, n - 1);
    m_submodeCbo->setCurrentIndex(cur);
}

void MainWindow::transmitNextFrame()
{
    if (m_pendingTxFrames.isEmpty()) return;
    if (m_transmitting) return;

    const QVariantMap &frame  = m_pendingTxFrames.first();
    const QString      payload   = frame[QStringLiteral("payload")].toString();
    const int          frameType = frame[QStringLiteral("frameType")].toInt();
    const int          submodeInt = frame[QStringLiteral("submode")].toInt();

    ModemTxFrame txf;
    txf.payload   = payload.toStdString();
    txf.frameType = frameType;
    txf.submode   = submodeInt;
    auto pcmNative = m_modem->modulate(txf, m_config.txFreqHz);
    if (!pcmNative.empty()) {
        const int ratio = 48000 / m_modem->sampleRate();
        std::vector<int16_t> pcm48k;
        pcm48k.reserve(pcmNative.size() * ratio);
        for (size_t i = 0; i + 1 < pcmNative.size(); ++i) {
            for (int j = 0; j < ratio; ++j) {
                float t = j / static_cast<float>(ratio);
                float s = pcmNative[i] * (1.0f - t) + pcmNative[i + 1] * t;
                pcm48k.push_back(static_cast<int16_t>(
                    std::clamp(s * 32767.0f, -32767.0f, 32767.0f)));
            }
        }
        float last = pcmNative.back();
        for (int j = 0; j < ratio; ++j)
            pcm48k.push_back(static_cast<int16_t>(
                std::clamp(last * 32767.0f, -32767.0f, 32767.0f)));

        setTransmitting(true);
        QMetaObject::invokeMethod(m_hamlib, [this]() {
            m_hamlib->setPtt(true);
        }, Qt::QueuedConnection);
        m_audioOut->play(std::move(pcm48k));
    }
    m_pendingTxFrames.removeFirst();
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void MainWindow::onSpectrumReady(std::vector<float> magnitudes, float sampleRateHz)
{
    m_latestSpectrum     = magnitudes;
    m_latestSpectrumRate = sampleRateHz;
    if (m_waterfall)
        m_waterfall->addLine(magnitudes, sampleRateHz);
    if (m_wsServer)
        m_wsServer->pushSpectrum(magnitudes, sampleRateHz);
}

void MainWindow::onPeriodStarted(int utc)
{
    // Trigger decode with the current ring buffer contents
    if (!m_transmitting && m_audioIn->isRunning()) {
        int utcCode = utc;
        auto samples = m_audioIn->takePeriodBuffer(utcCode);
        if (!samples.empty()) {
            QByteArray ba(reinterpret_cast<const char *>(samples.data()),
                          static_cast<int>(samples.size() * sizeof(int16_t)));
            emit requestDecode(ba, utcCode, 0);
        }
    }

    // Advance TX queue
    transmitNextFrame();

    ++m_periodCount;
}

void MainWindow::onPlaybackFinished()
{
    // PTT off
    QMetaObject::invokeMethod(m_hamlib, [this]() {
        m_hamlib->setPtt(false);
    }, Qt::QueuedConnection);

    setTransmitting(false);
    // For streaming modems, fire the next queued frame immediately.
    // For period-based modems it will be picked up at the next period boundary.
    const bool streaming = m_modem && (m_modem->submodeParms(0).periodSeconds == 0);
    if (streaming && !m_pendingTxFrames.isEmpty())
        transmitNextFrame();
}

void MainWindow::onAudioChunkReady(QByteArray chunk)
{
    if (m_transmitting) return;  // half-duplex: skip decode during TX
    emit requestDecode(chunk, 0, 0);
}

void MainWindow::onStreamTxTimer()
{
    if (!m_transmitting && !m_pendingTxFrames.isEmpty())
        transmitNextFrame();
}

void MainWindow::onDecodeFinished(const QList<QVariantMap> &results)
{
    // Deduplicate across submode passes AND across consecutive period boundaries.
    //
    // The decoder runs all submodes so the same transmission can appear multiple
    // times within a single call (different raw payloads, same human-readable
    // text).  It can also appear again in the next period's call because the
    // modem's detection window overlaps period boundaries.
    //
    // Fix: a persistent cache keyed on (rawText|rounded-freq).  Any entry seen
    // within the last 30 seconds is a duplicate and is silently dropped.
    // 30 s covers one Normal period (15 s) plus margin, yet is short enough
    // that a legitimate re-transmission of the same text (e.g. a second @HB
    // from the same station) will appear after the window expires.

    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();

    // Prune stale entries (keep the map from growing without bound)
    for (auto it = m_recentDecodes.begin(); it != m_recentDecodes.end(); ) {
        if (nowSec - it.value() > 30)
            it = m_recentDecodes.erase(it);
        else
            ++it;
    }

    for (const QVariantMap &m : results) {
        const QString rawT  = m[QStringLiteral("rawText")].toString().trimmed();
        const int     fKey  = qRound(m[QStringLiteral("freqHz")].toFloat() / 10.0f);
        const QString msgKey = rawT.isEmpty()
            ? m[QStringLiteral("message")].toString()
            : rawT + QLatin1Char('|') + QString::number(fKey);
        if (m_recentDecodes.contains(msgKey)) continue;
        m_recentDecodes.insert(msgKey, nowSec);
        const QString rawText   = m[QStringLiteral("rawText")].toString();
        const float   freqHz    = m[QStringLiteral("freqHz")].toFloat();
        int           snrDb     = m[QStringLiteral("snrDb")].toInt();
        const int     submode   = m[QStringLiteral("submode")].toInt();

        ModemDecoded d;
        d.message     = m[QStringLiteral("message")].toString().toStdString();
        d.frequencyHz = freqHz;
        d.snrDb       = snrDb;
        d.submode     = submode;
        d.frameType   = m[QStringLiteral("frameType")].toInt();
        d.modemType   = m[QStringLiteral("modemType")].toInt();
        d.isRawText   = m[QStringLiteral("isRawText")].toBool();

        // ── GFSK8 multi-frame assembly ────────────────────────────────────────
        // isSyncMark / isEom: handle before normal message processing.
        const bool isSyncMark = m[QStringLiteral("isSyncMark")].toBool();
        const bool isEom      = m[QStringLiteral("isEom")].toBool();

        if (isSyncMark) {
            // Sync heartbeat: nothing to display, could track timing here.
            continue;
        }

        // Determine frame position within a GFSK8 multi-frame message.
        // (fKey is already computed above for dedup; reuse it here.)
        const bool isGfsk8      = (d.modemType == 0);
        const bool frameIsFirst = isGfsk8 && ((d.frameType & Varicode::JS8CallFirst) != 0);
        const bool frameIsLast  = isGfsk8 && ((d.frameType & Varicode::JS8CallLast) != 0);
        // FrameDirected (3) = First|Last = single-frame; no assembly needed.
        const bool isSingleFrame = frameIsFirst && frameIsLast;

        QString effectiveRawText = rawText.isEmpty()
            ? QString::fromStdString(d.message) : rawText;

        if (isGfsk8 && frameIsFirst && !frameIsLast) {
            // First frame of a multi-frame message: start assembly buffer.
            GfskFrameBuffer buf;
            buf.assembledRawText = effectiveRawText;
            buf.firstSeenSec     = nowSec;
            buf.freqHz           = freqHz;
            buf.snrDb            = snrDb;
            buf.submode          = submode;
            m_gfsk8FrameBuffers[fKey] = buf;
            // Fall through to heard pane update only (no model/wsServer/auto-reply yet).
            // Parse just enough to get display text for heard pane.
            JS8Message partialMsg = parseDecoded(d, effectiveRawText, m_config.callsign);
            // Heard pane update for partial frame (no EOM marker yet)
            if (m_infoPane) {
                const QString display = partialMsg.rawText.isEmpty()
                    ? partialMsg.body : partialMsg.rawText;
                const QString newText = partialMsg.grid.isEmpty()
                    ? display : QStringLiteral("%1  {%2}").arg(display, partialMsg.grid);
                auto it2 = m_heardFreqBlock.find(fKey);
                if (it2 != m_heardFreqBlock.end()) {
                    QTextBlock block = m_infoPane->document()->findBlockByNumber(it2.value());
                    if (block.isValid()) {
                        QTextCursor cur(block);
                        cur.movePosition(QTextCursor::EndOfBlock);
                        cur.insertText(QStringLiteral("  |  ") + newText);
                    } else {
                        it2 = m_heardFreqBlock.end();
                    }
                }
                if (it2 == m_heardFreqBlock.end()) {
                    const int blockIndex = m_infoPane->document()->blockCount();
                    m_infoPane->appendPlainText(newText);
                    m_heardFreqBlock[fKey] = blockIndex;
                }
            }
            if (m_wsServer)
                m_wsServer->pushMessageFrame(freqHz, snrDb, submode, d.modemType,
                                             d.frameType, effectiveRawText,
                                             effectiveRawText,
                                             QDateTime::currentDateTimeUtc());
            continue; // skip model/wsServer/auto-reply
        }

        if (isGfsk8 && !frameIsFirst && !frameIsLast) {
            // Middle continuation frame: append to buffer.
            if (m_gfsk8FrameBuffers.contains(fKey))
                m_gfsk8FrameBuffers[fKey].assembledRawText += effectiveRawText;
            const QString assembled = m_gfsk8FrameBuffers.contains(fKey)
                ? m_gfsk8FrameBuffers[fKey].assembledRawText : effectiveRawText;
            // Heard pane: append to existing line.
            if (m_infoPane) {
                auto it2 = m_heardFreqBlock.find(fKey);
                if (it2 != m_heardFreqBlock.end()) {
                    QTextBlock block = m_infoPane->document()->findBlockByNumber(it2.value());
                    if (block.isValid()) {
                        QTextCursor cur(block);
                        cur.movePosition(QTextCursor::EndOfBlock);
                        cur.insertText(effectiveRawText);
                    }
                }
            }
            if (m_wsServer)
                m_wsServer->pushMessageFrame(freqHz, snrDb, submode, d.modemType,
                                             d.frameType, effectiveRawText,
                                             assembled,
                                             QDateTime::currentDateTimeUtc());
            continue; // skip model/wsServer/auto-reply
        }

        if (isGfsk8 && !frameIsFirst && frameIsLast) {
            // Last frame of multi-frame: assemble and process complete message.
            if (m_gfsk8FrameBuffers.contains(fKey)) {
                effectiveRawText = m_gfsk8FrameBuffers[fKey].assembledRawText + effectiveRawText;
                // Restore first-frame SNR for the assembled message
                snrDb = m_gfsk8FrameBuffers[fKey].snrDb;
                d.snrDb = snrDb;
                m_gfsk8FrameBuffers.remove(fKey);
            }
            d.frameType = Varicode::JS8CallLast;
            // Fall through to full processing below.
        }
        // else: isSingleFrame (FrameDirected=3) or streaming modem — process immediately.

        JS8Message msg = parseDecoded(d, effectiveRawText, m_config.callsign);
        // ── END GFSK8 multi-frame assembly ────────────────────────────────────

        // Update / fill grid from persistent cache
        if (!msg.from.isEmpty()) {
            GridCache &gc = GridCache::instance();
            if (!msg.grid.isEmpty()) {
                gc.set(msg.from, msg.grid);          // learn new grid
                msg.gridFromCache = false;           // heard live
            } else {
                const QString cached = gc.get(msg.from);
                if (!cached.isEmpty()) {
                    msg.grid = cached;
                    msg.gridFromCache = true;        // from cache, not live
                }
            }
        }

        // Calculate grid distance/bearing from user's grid
        if (!msg.grid.isEmpty() && !m_config.grid.isEmpty()) {
            calcDistBearing(m_config.grid, msg.grid, msg.distKm, msg.bearingDeg);
        }

        // Only add a new row when we know the sender — continuation frames
        // (non-first frames of multi-frame transmissions) carry no callsign
        // and create confusing anonymous rows in the table.
        if (!msg.from.isEmpty())
            m_model->addMessage(msg);
        if (m_wsServer)
            m_wsServer->pushMessageDecoded(msg);

        // PSK Reporter: submit spot for every decode with a known callsign.
        // Dial frequency (kHz) + audio offset (Hz) gives spot frequency in Hz.
        if (m_config.pskReporterEnabled && m_pskReporter && !msg.from.isEmpty()) {
            const quint64 spotFreqHz =
                static_cast<quint64>(m_config.frequencyKhz * 1000.0 + msg.audioFreqHz);
            m_pskReporter->addSpot(msg.from, msg.grid.isEmpty() ? msg.gridFromCache ? msg.grid : QString() : msg.grid,
                                   spotFreqHz, snrDb, msg.utc);
        }

        // Heard pane: group messages on the same frequency onto the same line.
        // Round to nearest 10 Hz as the grouping key.
        if (m_infoPane) {
            const QString display = msg.rawText.isEmpty() ? msg.body : msg.rawText;
            const QString newText = msg.grid.isEmpty()
                ? display
                : QStringLiteral("%1  {%2}").arg(display, msg.grid);
            const int freqKey = static_cast<int>(std::round(msg.audioFreqHz / 10.0f));

            auto it = m_heardFreqBlock.find(freqKey);
            if (it != m_heardFreqBlock.end()) {
                // Append to existing line for this frequency
                QTextBlock block = m_infoPane->document()->findBlockByNumber(it.value());
                if (block.isValid()) {
                    QTextCursor cur(block);
                    cur.movePosition(QTextCursor::EndOfBlock);
                    cur.insertText(QStringLiteral("  |  ") + newText);
                } else {
                    it = m_heardFreqBlock.end();  // block gone — fall through to new line
                }
            }
            if (it == m_heardFreqBlock.end()) {
                // New line: [time +freq] text
                const QString line = QStringLiteral("[%1 +%2] %3")
                    .arg(msg.utc.toString(QStringLiteral("HH:mm:ss")))
                    .arg(static_cast<int>(msg.audioFreqHz))
                    .arg(newText);
                m_infoPane->appendPlainText(line);
                m_heardFreqBlock[freqKey] = m_infoPane->document()->blockCount() - 1;
            }
        }

        // Append EOM marker to the heard pane line when a message is complete.
        const bool showEom = isEom
            || (isGfsk8 && (frameIsLast || isSingleFrame));
        if (showEom && m_infoPane) {
            const int eomFreqKey = static_cast<int>(std::round(
                (isGfsk8 ? freqHz : msg.audioFreqHz) / 10.0f));
            auto eomIt = m_heardFreqBlock.find(eomFreqKey);
            if (eomIt != m_heardFreqBlock.end()) {
                QTextBlock block = m_infoPane->document()->findBlockByNumber(eomIt.value());
                if (block.isValid()) {
                    QTextCursor cur(block);
                    cur.movePosition(QTextCursor::EndOfBlock);
                    cur.insertText(QStringLiteral(" \u2666")); // ♦
                }
            }
        }

        // Interactive pane: show messages within ±100 Hz of the TX/RX offset.
        // In focus mode (callsign selected), only show messages from that station.
        if (m_interactiveDisplay) {
            constexpr float kOffsetTolerance = 100.0f;
            const bool nearOffset = std::abs(msg.audioFreqHz - static_cast<float>(m_config.txFreqHz))
                    < kOffsetTolerance;
            const bool showInInteractive = nearOffset &&
                (m_selectedCallsign.isEmpty() ||
                 m_selectedCallsign == QStringLiteral("@ALL") ||
                 msg.from.toUpper() == m_selectedCallsign.toUpper() ||
                 msg.isAddressedToMe(m_config.callsign));
            if (showInInteractive) {
                const QString line = QStringLiteral("[%1] %2")
                    .arg(msg.utc.toString(QStringLiteral("HH:mm:ss")))
                    .arg(msg.rawText.isEmpty() ? msg.body : msg.rawText);
                m_interactiveDisplay->appendPlainText(line);
            }
        }

        // Focus mode: when a callsign is selected, only auto-reply to that station.
        // When @ALL is selected, suppress all auto-replies.
        const bool focusMode = !m_selectedCallsign.isEmpty();
        const bool focusMatch = focusMode &&
            m_selectedCallsign != QStringLiteral("@ALL") &&
            msg.from.toUpper() == m_selectedCallsign.toUpper();
        const bool allowAutoReply = !focusMode || focusMatch;

        // Auto-reply handling
        if (m_config.autoReply && allowAutoReply && msg.isAddressedToMe(m_config.callsign)) {
            if (msg.type == JS8Message::Type::SnrQuery) {
                sendAutoReply(msg.from, QStringLiteral("SNR %1 dB").arg(snrDb), snrDb);
            } else if (msg.type == JS8Message::Type::InfoQuery) {
                const QString info = m_config.stationInfo.isEmpty()
                    ? QStringLiteral("%1/%2/%3/JF8Call %4")
                        .arg(m_config.callsign)
                        .arg(m_config.grid)
                        .arg(m_submodeCbo->currentText().split(QStringLiteral(" ")).first())
                        .arg(JF8CALL_VERSION_STR)
                    : m_config.stationInfo;
                sendAutoReply(msg.from, QStringLiteral("INFO ") + info, snrDb);
            } else if (msg.type == JS8Message::Type::StatusQuery) {
                const QString status = m_config.stationStatus.isEmpty()
                    ? QStringLiteral("HEARD")
                    : QStringLiteral("STATUS ") + m_config.stationStatus;
                sendAutoReply(msg.from, status, snrDb);
            } else if (msg.type == JS8Message::Type::GridQuery) {
                sendAutoReply(msg.from, QStringLiteral("GRID ") + m_config.grid, snrDb);
            } else if (msg.type == JS8Message::Type::HearingQuery) {
                // Collect up to 10 recently heard unique callsigns
                QStringList heard;
                const int mc = m_model->messageCount();
                for (int hi = 0; hi < mc && heard.size() < 10; ++hi) {
                    const JS8Message &hm = m_model->messageAt(hi);
                    if (!hm.from.isEmpty() && !heard.contains(hm.from))
                        heard.append(hm.from);
                }
                sendAutoReply(msg.from,
                    QStringLiteral("HEARING ") + (heard.isEmpty()
                        ? QStringLiteral("NONE") : heard.join(QLatin1Char(' '))),
                    snrDb);
            } else if (msg.type == JS8Message::Type::QueryMsgs) {
                // Check for undelivered relay messages we hold for this station
                const auto relay = MessageInbox::instance().relayMessagesFor(msg.from);
                if (relay.isEmpty()) {
                    sendAutoReply(msg.from, QStringLiteral("NO"), snrDb);
                } else {
                    sendAutoReply(msg.from,
                        QStringLiteral("YES MSG ID %1").arg(relay.first().id), snrDb);
                }
            } else if (msg.type == JS8Message::Type::QueryMsg) {
                // "QUERY MSG <id>" — retrieve specific relay message
                const QString bodyUp = msg.body.toUpper();
                const int spPos = bodyUp.lastIndexOf(QLatin1Char(' '));
                bool ok = false;
                const int reqId = (spPos >= 0) ? msg.body.mid(spPos + 1).toInt(&ok) : -1;
                if (ok && reqId > 0) {
                    for (const InboxMessage &im : MessageInbox::instance().messages()) {
                        if (im.id == reqId && im.to.toUpper() == msg.from.toUpper()) {
                            sendAutoReply(msg.from,
                                QStringLiteral("MSG %1 FROM %2").arg(im.text, im.from),
                                snrDb);
                            MessageInbox::instance().markDelivered(reqId);
                            break;
                        }
                    }
                }
            }
        }

        // Auto-ACK: only for complete MSG commands (last or only frame).
        // Generic directed freetext is not auto-ACK'd per JS8 protocol.
        // JS8CallLast (bit 1) is set on the final frame of a multi-frame message,
        // and on FrameDirected (=3) which is a single-frame message.
        const bool isLastFrame = (d.frameType & Varicode::JS8CallLast) != 0;
        if (isLastFrame && msg.isAddressedToMe(m_config.callsign) && !msg.from.isEmpty()) {
            if (msg.type == JS8Message::Type::MsgCommand) {
                const QString ack = msg.from + QStringLiteral(" ") + m_config.callsign
                                   + QStringLiteral(": ACK");
                transmitMessage(ack);
            }
        }

        // MSG storage: store MsgCommand messages for this station or for relay.
        if (msg.type == JS8Message::Type::MsgCommand && !msg.from.isEmpty()) {
            // Strip "MSG " prefix from body to get message text
            const QString msgText = msg.body.startsWith(QStringLiteral("MSG "), Qt::CaseInsensitive)
                ? msg.body.mid(4).trimmed() : msg.body;
            InboxMessage im;
            im.utc     = msg.utc;
            im.from    = msg.from;
            im.to      = msg.to;
            im.text    = msgText;
            im.freqHz  = msg.audioFreqHz;
            im.snrDb   = snrDb;
            im.read    = false;
            im.delivered = false;
            MessageInbox::instance().store(im);
            updateInboxNotification();
        }

        ++m_decodeCount;
    }
    m_countLabel->setText(QStringLiteral("%1 decoded").arg(m_decodeCount));
}

// Returns true when word could plausibly be a ham callsign (has both letters
// and digits, all chars are alphanumeric or '/').  Used to detect directed
// messages typed directly into the TX field.
static bool looksLikeCallsign(const QString &word)
{
    if (word.length() < 3 || word.length() > 12) return false;
    bool hasLetter = false, hasDigit = false;
    for (const QChar &c : word) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('/')) return false;
        if (c.isLetter()) hasLetter = true;
        if (c.isDigit())  hasDigit  = true;
    }
    return hasLetter && hasDigit;
}

void MainWindow::sendAutoReply(const QString &toCall, const QString &body, int /*snrDb*/)
{
    if (m_config.callsign.isEmpty()) return;
    // Directed format: TOCALL MYCALL: body
    const QString text = toCall + QStringLiteral(" ") + m_config.callsign
                       + QStringLiteral(": ") + body;
    transmitMessage(text);
}

void MainWindow::onSendClicked()
{
    const QString text = m_txEdit->text().trimmed();
    if (text.isEmpty()) return;

    // If a specific callsign is selected in the Info pane (focus mode),
    // direct the message to that station unless the user already put a callsign
    // at the start of the text.
    if (!m_selectedCallsign.isEmpty() && m_selectedCallsign != QStringLiteral("@ALL")) {
        const int sp = text.indexOf(QLatin1Char(' '));
        const bool alreadyDirected = (sp > 0 &&
            looksLikeCallsign(text.left(sp).toUpper()));
        if (!alreadyDirected) {
            transmitMessage(m_selectedCallsign + QStringLiteral(" ") + m_config.callsign
                            + QStringLiteral(": ") + text);
            m_txEdit->clear();
            return;
        }
    }

    // If text starts with something that looks like a callsign followed by a
    // message body, format it as a directed message: DEST MYCALL: body.
    // A lone callsign (no body) is sent as-is (undirected).
    const int sp = text.indexOf(QLatin1Char(' '));
    if (sp > 0 && looksLikeCallsign(text.left(sp).toUpper())) {
        const QString dest = text.left(sp).toUpper();
        const QString body = text.mid(sp + 1).trimmed();
        transmitMessage(dest + QStringLiteral(" ") + m_config.callsign
                        + QStringLiteral(": ") + body);
    } else {
        transmitMessage(text);
    }
    m_txEdit->clear();
}

void MainWindow::onInfoTableClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    QString call;
    if (index.row() == 0) {
        call = QStringLiteral("@ALL");
    } else {
        const int msgIdx = index.row() - 1;
        if (msgIdx < 0 || msgIdx >= m_model->messageCount()) return;
        call = m_model->messageAt(msgIdx).from;
        if (call.isEmpty()) return;
    }
    m_selectedCallsign = call.toUpper();
    m_deselectBtn->setEnabled(true);
    m_msgBtn->setEnabled(m_selectedCallsign != QStringLiteral("@ALL"));
    const QString placeholder = m_selectedCallsign == QStringLiteral("@ALL")
        ? tr("Undirected TX (broadcast)")
        : tr("Directed to %1 — Enter to send").arg(m_selectedCallsign);
    m_txEdit->setPlaceholderText(placeholder);
}

void MainWindow::onDeselectClicked()
{
    m_selectedCallsign.clear();
    m_messageTable->clearSelection();
    m_deselectBtn->setEnabled(false);
    m_msgBtn->setEnabled(false);
    m_txEdit->setPlaceholderText(tr("TX on offset Hz — Enter to send"));
}

void MainWindow::onMsgBtnClicked()
{
    if (m_selectedCallsign.isEmpty() || m_selectedCallsign == QStringLiteral("@ALL")) {
        statusBar()->showMessage(tr("Select a callsign in the Info pane first"), 3000);
        return;
    }
    m_txEdit->setText(m_selectedCallsign + QStringLiteral(" ") + m_config.callsign
                      + QStringLiteral(": MSG "));
    m_txEdit->setFocus();
    m_txEdit->setCursorPosition(m_txEdit->text().length());
}

void MainWindow::updateInboxNotification()
{
    if (!m_inboxNotifyBtn) return;
    const int n = MessageInbox::instance().unreadCount(m_config.callsign);
    if (n > 0) {
        m_inboxNotifyBtn->setText(QStringLiteral("Inbox (%1)").arg(n));
        m_inboxNotifyBtn->show();
    } else {
        m_inboxNotifyBtn->hide();
    }
}

void MainWindow::onOpenInbox()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Message Inbox — JF8Call"));
    dlg.setMinimumSize(640, 400);

    auto *vbox = new QVBoxLayout(&dlg);

    auto *table = new QTableWidget(0, 5, &dlg);
    table->setHorizontalHeaderLabels({tr("Time"), tr("From"), tr("To"), tr("Text"), tr("Status")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->setColumnWidth(0, 70);
    table->setColumnWidth(1, 70);
    table->setColumnWidth(2, 70);
    table->setColumnWidth(4, 70);
    vbox->addWidget(table, 1);

    // Populate from inbox
    auto refresh = [&]() {
        table->setRowCount(0);
        const auto &msgs = MessageInbox::instance().messages();
        for (const InboxMessage &m : msgs) {
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(m.utc.toString(QStringLiteral("HH:mm"))));
            table->setItem(row, 1, new QTableWidgetItem(m.from));
            table->setItem(row, 2, new QTableWidgetItem(m.to));
            table->setItem(row, 3, new QTableWidgetItem(m.text));
            const QString status = m.delivered ? tr("Delivered")
                : (m.read ? tr("Read") : tr("Unread"));
            auto *statusItem = new QTableWidgetItem(status);
            statusItem->setData(Qt::UserRole, m.id);
            table->setItem(row, 4, statusItem);
        }
    };
    refresh();

    auto *btnRow = new QHBoxLayout;
    auto *replyBtn = new QPushButton(tr("Reply"), &dlg);
    auto *deleteBtn = new QPushButton(tr("Delete"), &dlg);
    auto *closeBtn = new QPushButton(tr("Close"), &dlg);
    btnRow->addWidget(replyBtn);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    vbox->addLayout(btnRow);

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    connect(replyBtn, &QPushButton::clicked, &dlg, [&]() {
        const int row = table->currentRow();
        if (row < 0) return;
        const int id = table->item(row, 4)->data(Qt::UserRole).toInt();
        const QString from = table->item(row, 1)->text();
        MessageInbox::instance().markRead(id);
        dlg.accept();
        // Pre-fill TX box with reply
        m_txEdit->setText(from + QStringLiteral(" ") + m_config.callsign
                          + QStringLiteral(": "));
        m_txEdit->setFocus();
        m_txEdit->setCursorPosition(m_txEdit->text().length());
        updateInboxNotification();
    });

    connect(deleteBtn, &QPushButton::clicked, &dlg, [&]() {
        const int row = table->currentRow();
        if (row < 0) return;
        const int id = table->item(row, 4)->data(Qt::UserRole).toInt();
        MessageInbox::instance().remove(id);
        refresh();
        updateInboxNotification();
    });

    // Mark selected message read when it's selected
    connect(table, &QTableWidget::currentCellChanged, &dlg,
            [&](int row, int, int, int) {
        if (row < 0) return;
        const int id = table->item(row, 4)->data(Qt::UserRole).toInt();
        MessageInbox::instance().markRead(id);
        table->item(row, 4)->setText(tr("Read"));
        updateInboxNotification();
    });

    dlg.exec();
    updateInboxNotification();
}

void MainWindow::onHbClicked()
{
    const QString grid4 = m_config.grid.left(4);
    transmitMessage(m_config.callsign + QStringLiteral(": @HB HEARTBEAT ") + grid4, grid4);
}

void MainWindow::onSnrQueryClicked()
{
    const QString dest = !m_selectedCallsign.isEmpty() && m_selectedCallsign != QStringLiteral("@ALL")
                         ? m_selectedCallsign
                         : m_txEdit->text().trimmed().toUpper();
    if (dest.isEmpty()) {
        statusBar()->showMessage(tr("Select a callsign or enter one in the TX box first"), 3000);
        return;
    }
    transmitMessage(dest + QStringLiteral(" ") + m_config.callsign + QStringLiteral(": SNR?"));
}

void MainWindow::onInfoQueryClicked()
{
    const QString dest = !m_selectedCallsign.isEmpty() && m_selectedCallsign != QStringLiteral("@ALL")
                         ? m_selectedCallsign
                         : m_txEdit->text().trimmed().toUpper();
    if (dest.isEmpty()) {
        statusBar()->showMessage(tr("Select a callsign or enter one in the TX box first"), 3000);
        return;
    }
    transmitMessage(dest + QStringLiteral(" ") + m_config.callsign + QStringLiteral(": INFO?"));
}

void MainWindow::onGridQueryClicked()
{
    const QString dest = !m_selectedCallsign.isEmpty() && m_selectedCallsign != QStringLiteral("@ALL")
                         ? m_selectedCallsign
                         : m_txEdit->text().trimmed().toUpper();
    if (dest.isEmpty()) {
        statusBar()->showMessage(tr("Select a callsign or enter one in the TX box first"), 3000);
        return;
    }
    transmitMessage(dest + QStringLiteral(" ") + m_config.callsign + QStringLiteral(": GRID?"));
}

void MainWindow::onStatusQueryClicked()
{
    const QString dest = !m_selectedCallsign.isEmpty() && m_selectedCallsign != QStringLiteral("@ALL")
                         ? m_selectedCallsign
                         : m_txEdit->text().trimmed().toUpper();
    if (dest.isEmpty()) {
        statusBar()->showMessage(tr("Select a callsign or enter one in the TX box first"), 3000);
        return;
    }
    transmitMessage(dest + QStringLiteral(" ") + m_config.callsign + QStringLiteral(": STATUS?"));
}

void MainWindow::onHearingQueryClicked()
{
    const QString dest = !m_selectedCallsign.isEmpty() && m_selectedCallsign != QStringLiteral("@ALL")
                         ? m_selectedCallsign
                         : m_txEdit->text().trimmed().toUpper();
    if (dest.isEmpty()) {
        statusBar()->showMessage(tr("Select a callsign or enter one in the TX box first"), 3000);
        return;
    }
    transmitMessage(dest + QStringLiteral(" ") + m_config.callsign + QStringLiteral(": HEARING?"));
}

void MainWindow::onCqClicked()
{
    if (m_config.cqMessage.isEmpty()) {
        statusBar()->showMessage(tr("Set CQ message in Preferences first"), 3000);
        return;
    }
    transmitMessage(m_config.cqMessage);
}

void MainWindow::onHaltClicked()
{
    m_pendingTxFrames.clear();
    if (m_transmitting) {
        m_audioOut->close();
        m_audioOut->open(m_config.audioOutputName);
        QMetaObject::invokeMethod(m_hamlib, [this]() {
            m_hamlib->setPtt(false);
        }, Qt::QueuedConnection);
        setTransmitting(false);
    }
    statusBar()->showMessage(tr("TX halted"), 3000);
    if (m_wsServer) m_wsServer->pushTxQueued(0);
}

void MainWindow::transmitMessage(const QString &text, const QString &gridOverride)
{
    if (m_config.callsign.isEmpty()) {
        statusBar()->showMessage(tr("Set your callsign first"), 3000);
        return;
    }

    // Append CRC-16/KERMIT checksum to directed messages.
    // A directed message starts with a destination callsign that differs from
    // our own callsign, e.g. "W5XYZ N0GQ: hello".  ACK messages are excluded
    // (short, unambiguous; checksumming them would waste frame space).
    QString txText = text;
    {
        const int sp = text.indexOf(QLatin1Char(' '));
        if (sp > 0) {
            const QString dest = text.left(sp).toUpper();
            if (dest != m_config.callsign.toUpper()) {
                // It's directed to another station — add checksum unless it's ACK
                const int ci = text.indexOf(QStringLiteral(": "));
                const QString body = (ci >= 0) ? text.mid(ci + 2).trimmed() : QString();
                if (body.toUpper() != QStringLiteral("ACK")) {
                    txText = JS8Checksum::appendChecksum(text);
                }
            }
        }
    }

    const QString grid = gridOverride.isEmpty() ? m_config.grid : gridOverride;
    auto frames = m_modem->pack(m_config.callsign.toStdString(),
                                grid.toStdString(),
                                txText.toStdString(), m_config.submode);
    if (frames.empty()) {
        statusBar()->showMessage(tr("Failed to encode message"), 3000);
        return;
    }

    for (const ModemTxFrame &f : frames) {
        QVariantMap m;
        m[QStringLiteral("payload")]   = QString::fromStdString(f.payload);
        m[QStringLiteral("frameType")] = f.frameType;
        m[QStringLiteral("submode")]   = f.submode;
        m_pendingTxFrames.append(m);
    }

    // Show outgoing text in the interactive pane
    if (m_interactiveDisplay) {
        const QString line = QStringLiteral("[%1] >> %2")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss")))
            .arg(text);
        m_interactiveDisplay->appendPlainText(line);
    }

    if (m_wsServer) m_wsServer->pushTxQueued(m_pendingTxFrames.size());
    statusBar()->showMessage(
        QStringLiteral("Queued %1 frame(s) for TX").arg(frames.size()), 3000);
}

void MainWindow::setTransmitting(bool tx)
{
    m_transmitting = tx;
    if (tx) {
        m_rxTxLabel->setText(tr("TX"));
        m_rxTxLabel->setStyleSheet(QStringLiteral("color: #cc2222; font-weight: bold;"));
        if (m_wsServer) m_wsServer->pushTxStarted();
    } else {
        m_rxTxLabel->setText(tr("RX"));
        m_rxTxLabel->setStyleSheet(QStringLiteral("color: #7fbf7f; font-weight: bold;"));
        if (m_wsServer) m_wsServer->pushTxFinished();
    }
}

void MainWindow::onHeartbeatCheck()
{
    if (!m_config.heartbeatEnabled) return;
    if (m_config.callsign.isEmpty()) return;
    const int period = m_modem->submodeParms(m_config.submode).periodSeconds;
    // For streaming modems (period==0), use 30s as heartbeat base period.
    const int effectivePeriod = (period > 0) ? period : 30;
    // Fire heartbeat every heartbeatIntervalPeriods periods (approximate — count seconds)
    static int secs = 0;
    ++secs;
    if (secs >= effectivePeriod * m_config.heartbeatIntervalPeriods) {
        secs = 0;
        const QString grid4 = m_config.grid.left(4);
        transmitMessage(m_config.callsign + QStringLiteral(": @HB HEARTBEAT ") + grid4, grid4);
    }
}

// Build a RigConfig from the current persisted settings.
RigConfig MainWindow::configToRigConfig() const
{
    RigConfig cfg;
    cfg.rigModel  = m_config.rigModel;
    cfg.port      = m_config.rigPort;
    cfg.baudRate  = m_config.rigBaud;
    cfg.dataBits  = m_config.rigDataBits;
    cfg.stopBits  = m_config.rigStopBits;
    cfg.parity    = m_config.rigParity;
    cfg.handshake = m_config.rigHandshake;
    cfg.dtrState  = m_config.rigDtrState;
    cfg.rtsState  = m_config.rigRtsState;
    cfg.pttType   = m_config.pttType;
    return cfg;
}

// Persist a RigConfig back into m_config.
void MainWindow::rigConfigToConfig(const RigConfig &cfg)
{
    m_config.rigModel    = cfg.rigModel;
    m_config.rigPort     = cfg.port;
    m_config.rigBaud     = cfg.baudRate;
    m_config.rigDataBits = cfg.dataBits;
    m_config.rigStopBits = cfg.stopBits;
    m_config.rigParity   = cfg.parity;
    m_config.rigHandshake= cfg.handshake;
    m_config.rigDtrState = cfg.dtrState;
    m_config.rigRtsState = cfg.rtsState;
    m_config.pttType     = cfg.pttType;
}

void MainWindow::onFrameCleanupTimer()
{
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_gfsk8FrameBuffers.begin(); it != m_gfsk8FrameBuffers.end(); ) {
        const qint64 ageSec = nowSec - it.value().firstSeenSec;
        if (ageSec > 90) {
            // Discard: too old to be useful
            it = m_gfsk8FrameBuffers.erase(it);
        } else if (ageSec > 60) {
            // Force-complete: process whatever was assembled so far
            const GfskFrameBuffer &buf = it.value();
            if (!buf.assembledRawText.isEmpty()) {
                ModemDecoded d;
                d.modemType   = 0;
                d.isRawText   = false;
                d.frequencyHz = buf.freqHz;
                d.snrDb       = buf.snrDb;
                d.submode     = buf.submode;
                d.frameType   = Varicode::JS8CallLast; // force-complete
                // Re-parse as assembled message
                JS8Message msg = parseDecoded(d,
                    buf.assembledRawText, m_config.callsign);
                if (!msg.from.isEmpty()) m_model->addMessage(msg);
                if (m_wsServer) m_wsServer->pushMessageDecoded(msg);
                // Append timeout marker to heard pane
                const int fKey = qRound(buf.freqHz / 10.0f);
                auto hIt = m_heardFreqBlock.find(fKey);
                if (hIt != m_heardFreqBlock.end()) {
                    QTextBlock block = m_infoPane->document()->findBlockByNumber(hIt.value());
                    if (block.isValid()) {
                        QTextCursor cur(block);
                        cur.movePosition(QTextCursor::EndOfBlock);
                        cur.insertText(QStringLiteral(" \u2666?"));
                    }
                }
            }
            it = m_gfsk8FrameBuffers.erase(it);
        } else {
            ++it;
        }
    }
}

void MainWindow::onRadioSetup()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Radio Setup \u2014 JF8Call"));
    dlg.setMinimumWidth(520);

    auto *vbox = new QVBoxLayout(&dlg);

    // ── Connection settings ─────────────────────────────────────────────────
    auto *connGroup = new QGroupBox(QStringLiteral("Radio Connection"), &dlg);
    auto *form = new QFormLayout(connGroup);

    // Rig model — searchable combo populated from Hamlib backend list
    auto *rigCombo = new QComboBox(connGroup);
    rigCombo->setMaxVisibleItems(20);
#ifdef HAVE_HAMLIB
    const QStringList rigs = m_hamlib->availableRigs();
    for (const QString &r : rigs) rigCombo->addItem(r);
    const QString savedRigPrefix = QString::number(m_config.rigModel) + QStringLiteral(" \u2014");
    for (int i = 0; i < rigCombo->count(); ++i) {
        if (rigCombo->itemText(i).startsWith(savedRigPrefix)) {
            rigCombo->setCurrentIndex(i); break;
        }
    }
#else
    rigCombo->addItem(QStringLiteral("(Hamlib not compiled in)"));
    rigCombo->setEnabled(false);
#endif
    form->addRow(QStringLiteral("Rig Model:"), rigCombo);

    // Port — populated from detected system interfaces; editable for manual entry
    auto *portCombo = new QComboBox(connGroup);
    portCombo->setEditable(true);
    const QStringList detectedPorts = detectSerialPorts();
    portCombo->addItems(detectedPorts);
    if (detectedPorts.isEmpty())
        portCombo->setPlaceholderText(QStringLiteral("No ports detected \u2014 type path here"));
    if (!m_config.rigPort.isEmpty()) {
        if (portCombo->findText(m_config.rigPort) == -1)
            portCombo->insertItem(0, m_config.rigPort);
        portCombo->setCurrentText(m_config.rigPort);
    }
    form->addRow(QStringLiteral("Port / Address:"), portCombo);

    // Baud rate
    auto *baudCombo = new QComboBox(connGroup);
    for (int b : {1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200})
        baudCombo->addItem(QString::number(b), b);
    baudCombo->setCurrentText(QString::number(m_config.rigBaud));
    form->addRow(QStringLiteral("Baud Rate:"), baudCombo);

    // Data bits
    auto *dataCombo = new QComboBox(connGroup);
    dataCombo->addItem(QStringLiteral("7"), 7);
    dataCombo->addItem(QStringLiteral("8"), 8);
    dataCombo->setCurrentText(QString::number(m_config.rigDataBits));
    form->addRow(QStringLiteral("Data Bits:"), dataCombo);

    // Stop bits
    auto *stopCombo = new QComboBox(connGroup);
    stopCombo->addItem(QStringLiteral("1"), 1);
    stopCombo->addItem(QStringLiteral("2"), 2);
    stopCombo->setCurrentText(QString::number(m_config.rigStopBits));
    form->addRow(QStringLiteral("Stop Bits:"), stopCombo);

    // Parity
    auto *parityCombo = new QComboBox(connGroup);
    parityCombo->addItem(QStringLiteral("None"),  0);
    parityCombo->addItem(QStringLiteral("Odd"),   1);
    parityCombo->addItem(QStringLiteral("Even"),  2);
    parityCombo->setCurrentIndex(m_config.rigParity);
    form->addRow(QStringLiteral("Parity:"), parityCombo);

    // Handshake
    auto *hsCombo = new QComboBox(connGroup);
    hsCombo->addItem(QStringLiteral("None"),               0);
    hsCombo->addItem(QStringLiteral("XON/XOFF"),           1);
    hsCombo->addItem(QStringLiteral("Hardware (RTS/CTS)"), 2);
    hsCombo->setCurrentIndex(m_config.rigHandshake);
    form->addRow(QStringLiteral("Handshake:"), hsCombo);

    // DTR
    auto *dtrCombo = new QComboBox(connGroup);
    dtrCombo->addItem(QStringLiteral("Unset"), 0);
    dtrCombo->addItem(QStringLiteral("On"),    1);
    dtrCombo->addItem(QStringLiteral("Off"),   2);
    dtrCombo->setCurrentIndex(m_config.rigDtrState);
    form->addRow(QStringLiteral("DTR:"), dtrCombo);

    // RTS
    auto *rtsCombo = new QComboBox(connGroup);
    rtsCombo->addItem(QStringLiteral("Unset"), 0);
    rtsCombo->addItem(QStringLiteral("On"),    1);
    rtsCombo->addItem(QStringLiteral("Off"),   2);
    rtsCombo->setCurrentIndex(m_config.rigRtsState);
    form->addRow(QStringLiteral("RTS:"), rtsCombo);

    // PTT type
    auto *pttCombo = new QComboBox(connGroup);
    pttCombo->addItem(QStringLiteral("VOX (radio handles it)"),     0);
    pttCombo->addItem(QStringLiteral("CAT (software command)"),     1);
    pttCombo->addItem(QStringLiteral("DTR (serial hardware line)"), 2);
    pttCombo->addItem(QStringLiteral("RTS (serial hardware line)"), 3);
    pttCombo->setCurrentIndex(m_config.pttType);
    form->addRow(QStringLiteral("PTT Keying:"), pttCombo);

    // Status
    auto *statusLbl = new QLabel(
        m_hamlib->isConnected() ? QStringLiteral("\u25cf Connected")
                                : QStringLiteral("\u25cb Not connected"), connGroup);
    statusLbl->setObjectName(QStringLiteral("radioStatusDlg"));
    form->addRow(QStringLiteral("Status:"), statusLbl);

    // Connect / Disconnect / Test buttons
    auto *connBtnRow = new QWidget(connGroup);
    auto *connBtnLayout = new QHBoxLayout(connBtnRow);
    connBtnLayout->setContentsMargins(0, 0, 0, 0);
    auto *connectBtn    = new QPushButton(QStringLiteral("Connect"),          connBtnRow);
    auto *disconnectBtn = new QPushButton(QStringLiteral("Disconnect"),       connBtnRow);
    auto *testBtn       = new QPushButton(QStringLiteral("Test Connection"),  connBtnRow);
    connBtnLayout->addWidget(connectBtn);
    connBtnLayout->addWidget(disconnectBtn);
    connBtnLayout->addWidget(testBtn);
    connBtnLayout->addStretch(1);
    form->addRow(connBtnRow);

    auto *testLbl = new QLabel(QString(), connGroup);
    testLbl->setWordWrap(true);
    form->addRow(QStringLiteral("Test result:"), testLbl);

    vbox->addWidget(connGroup);
    vbox->addStretch(1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vbox->addWidget(buttons);

    // ── Helper: build RigConfig from current UI state ───────────────────────
    auto uiToConfig = [&]() -> RigConfig {
        RigConfig cfg;
        cfg.port      = portCombo->currentText().trimmed();
        cfg.baudRate  = baudCombo->currentData().toInt();
        cfg.dataBits  = dataCombo->currentData().toInt();
        cfg.stopBits  = stopCombo->currentData().toInt();
        cfg.parity    = parityCombo->currentData().toInt();
        cfg.handshake = hsCombo->currentData().toInt();
        cfg.dtrState  = dtrCombo->currentData().toInt();
        cfg.rtsState  = rtsCombo->currentData().toInt();
        cfg.pttType   = pttCombo->currentData().toInt();
#ifdef HAVE_HAMLIB
        const QString rigText = rigCombo->currentText();
        bool ok = false;
        const int model = rigText.section(QLatin1Char(' '), 0, 0).toInt(&ok);
        cfg.rigModel = ok ? model : 1;
#endif
        return cfg;
    };

    // ── Wire up buttons ─────────────────────────────────────────────────────
    connect(connectBtn, &QPushButton::clicked, &dlg, [&]() {
        bool ok = false;
        QMetaObject::invokeMethod(m_hamlib, "connectRig", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(bool, ok), Q_ARG(RigConfig, uiToConfig()));
        statusLbl->setText(ok ? QStringLiteral("\u25cf Connected")
                              : QStringLiteral("\u25cb Connection failed"));
    });

    connect(disconnectBtn, &QPushButton::clicked, &dlg, [&]() {
        QMetaObject::invokeMethod(m_hamlib, "disconnectRig", Qt::BlockingQueuedConnection);
        statusLbl->setText(QStringLiteral("\u25cb Not connected"));
        testLbl->setText(QString());
    });

    connect(testBtn, &QPushButton::clicked, &dlg, [&]() {
        if (!m_hamlib->isConnected()) {
            bool ok = false;
            QMetaObject::invokeMethod(m_hamlib, "connectRig", Qt::BlockingQueuedConnection,
                Q_RETURN_ARG(bool, ok), Q_ARG(RigConfig, uiToConfig()));
            if (!ok) {
                testLbl->setText(QStringLiteral("\u2718 Connection failed"));
                return;
            }
            statusLbl->setText(QStringLiteral("\u25cf Connected"));
        }
        double khz = 0.0;
        QMetaObject::invokeMethod(m_hamlib, "getFrequency", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(double, khz));
        QString mode;
        QMetaObject::invokeMethod(m_hamlib, "getMode", Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(QString, mode));
        if (khz > 0)
            testLbl->setText(QStringLiteral("\u2714 OK \u2014 %1 kHz  %2").arg(khz, 0, 'f', 3).arg(mode));
        else
            testLbl->setText(QStringLiteral("\u2714 Connected but frequency read failed"));
    });

    if (dlg.exec() != QDialog::Accepted)
        return;

    rigConfigToConfig(uiToConfig());
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::onPreferences()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Preferences \u2014 JF8Call"));
    dlg.setMinimumWidth(480);

    // Scrollable content so the dialog works on small screens
    auto *scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    scroll->setWidget(content);
    auto *vbox = new QVBoxLayout(content);
    auto *outerVbox = new QVBoxLayout(&dlg);
    outerVbox->setContentsMargins(0, 0, 0, 0);
    outerVbox->addWidget(scroll);

    // ── Station identity ────────────────────────────────────────────────────
    auto *stationGroup = new QGroupBox(tr("Station Identity"), content);
    auto *stationForm  = new QFormLayout(stationGroup);

    auto *callEdit = new QLineEdit(m_config.callsign, stationGroup);
    callEdit->setPlaceholderText(QStringLiteral("W5XYZ"));
    stationForm->addRow(tr("Callsign:"), callEdit);

    auto *gridEdit = new QLineEdit(m_config.grid, stationGroup);
    gridEdit->setPlaceholderText(QStringLiteral("DM79AA"));
    stationForm->addRow(tr("Grid Square:"), gridEdit);

    auto *infoEdit = new QLineEdit(m_config.stationInfo, stationGroup);
    infoEdit->setPlaceholderText(tr("Optional custom info text (blank = auto-built)"));
    stationForm->addRow(tr("Info text:"), infoEdit);

    auto *statusEdit = new QLineEdit(m_config.stationStatus, stationGroup);
    statusEdit->setPlaceholderText(tr("Optional status text (blank = HEARD)"));
    stationForm->addRow(tr("Status text:"), statusEdit);

    auto *cqEdit = new QLineEdit(m_config.cqMessage, stationGroup);
    cqEdit->setPlaceholderText(tr("e.g. CQ CQ DE W5XYZ DM79"));
    stationForm->addRow(tr("CQ message:"), cqEdit);

    vbox->addWidget(stationGroup);

    // ── Audio ───────────────────────────────────────────────────────────────
    auto *audioGroup = new QGroupBox(tr("Audio"), content);
    auto *audioForm  = new QFormLayout(audioGroup);

    const QStringList inDevs  = AudioInput::availableDevices();
    const QStringList outDevs = AudioOutput::availableDevices();

    auto *inDevCombo = new QComboBox(audioGroup);
    inDevCombo->setEditable(true);
    inDevCombo->addItem(tr("(default)"), QString());
    for (const QString &d : inDevs) inDevCombo->addItem(d, d);
    {
        int idx = m_config.audioInputName.isEmpty()
            ? 0 : inDevCombo->findText(m_config.audioInputName);
        if (idx < 0) { inDevCombo->insertItem(1, m_config.audioInputName, m_config.audioInputName); idx = 1; }
        inDevCombo->setCurrentIndex(idx);
    }
    audioForm->addRow(tr("Input device:"), inDevCombo);

    auto *outDevCombo = new QComboBox(audioGroup);
    outDevCombo->setEditable(true);
    outDevCombo->addItem(tr("(default)"), QString());
    for (const QString &d : outDevs) outDevCombo->addItem(d, d);
    {
        int idx = m_config.audioOutputName.isEmpty()
            ? 0 : outDevCombo->findText(m_config.audioOutputName);
        if (idx < 0) { outDevCombo->insertItem(1, m_config.audioOutputName, m_config.audioOutputName); idx = 1; }
        outDevCombo->setCurrentIndex(idx);
    }
    audioForm->addRow(tr("Output device:"), outDevCombo);

    vbox->addWidget(audioGroup);

    // ── Modem ────────────────────────────────────────────────────────────────
    auto *modemGroup = new QGroupBox(tr("Modem"), content);
    auto *modemForm  = new QFormLayout(modemGroup);

    auto *modemTypeCbo = new QComboBox(modemGroup);
    modemTypeCbo->addItem(tr("JS8 / GFSK8 (HF digital)"), 0);
#ifdef HAVE_CODEC2
    modemTypeCbo->addItem(tr("Codec2 DATAC (HF data)"), 1);
#else
    modemTypeCbo->addItem(tr("Codec2 DATAC (not compiled in)"), 1);
    modemTypeCbo->setItemData(1, false, Qt::UserRole - 1);  // disable
#endif
#ifdef HAVE_OLIVIA
    modemTypeCbo->addItem(tr("Olivia (streaming HF)"), 2);
#else
    modemTypeCbo->addItem(tr("Olivia (not compiled in)"), 2);
    modemTypeCbo->setItemData(2, false, Qt::UserRole - 1);
#endif
#ifdef HAVE_PSK_MODEM
    modemTypeCbo->addItem(tr("PSK (streaming HF)"), 3);
#else
    modemTypeCbo->addItem(tr("PSK (not compiled in)"), 3);
    modemTypeCbo->setItemData(3, false, Qt::UserRole - 1);
#endif
    modemTypeCbo->setCurrentIndex(m_config.modemType);
    modemForm->addRow(tr("Modem type:"), modemTypeCbo);

    // Speed combo — populated dynamically based on modem type
    auto *modemSpeedCbo = new QComboBox(modemGroup);
    auto populateSpeedCbo = [&](int mtype) {
        modemSpeedCbo->clear();
        // Build a temporary modem to query submode names
        std::unique_ptr<IModem> tmp;
#ifdef HAVE_CODEC2
        if (mtype == 1)
            tmp = std::make_unique<Codec2Modem>(0);
        else
#endif
#ifdef HAVE_OLIVIA
        if (mtype == 2)
            tmp = std::make_unique<OliviaModem>(0);
        else
#endif
#ifdef HAVE_PSK_MODEM
        if (mtype == 3)
            tmp = std::make_unique<PskModem>(0);
        else
#endif
            tmp = std::make_unique<Gfsk8Modem>();
        const int n = tmp->submodeCount();
        for (int i = 0; i < n; ++i) {
            const auto &p = tmp->submodeParms(i);
            QString label = QString::fromStdString(p.name);
            if (p.periodSeconds > 0)
                label += QStringLiteral(" (%1s)").arg(p.periodSeconds);
            modemSpeedCbo->addItem(label);
        }
        // Select current submode when same modem type, else sensible default
        int def = 0;
#ifdef HAVE_PSK_MODEM
        if (mtype == 3) def = 1;  // PSK defaults to BPSK63 (index 1)
#endif
        const int cur = (mtype == m_config.modemType)
            ? std::clamp(m_config.submode, 0, n - 1) : def;
        modemSpeedCbo->setCurrentIndex(cur);
    };
    populateSpeedCbo(m_config.modemType);
    modemForm->addRow(tr("Speed:"), modemSpeedCbo);

    connect(modemTypeCbo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            modemGroup, [&, populateSpeedCbo](int idx) {
#ifndef HAVE_CODEC2
                if (idx == 1) { modemTypeCbo->setCurrentIndex(0); return; }
#endif
#ifndef HAVE_OLIVIA
                if (idx == 2) { modemTypeCbo->setCurrentIndex(0); return; }
#endif
#ifndef HAVE_PSK_MODEM
                if (idx == 3) { modemTypeCbo->setCurrentIndex(0); return; }
#endif
                populateSpeedCbo(modemTypeCbo->itemData(idx).toInt());
            });

    auto *modemNote = new QLabel(
        tr("<i>Changing the modem type restarts the audio chain.</i>"), modemGroup);
    modemNote->setWordWrap(true);
    modemNote->setStyleSheet(QStringLiteral("color: #c9a84c;"));
    modemForm->addRow(modemNote);

    vbox->addWidget(modemGroup);

    // ── Operating ───────────────────────────────────────────────────────────
    auto *opGroup = new QGroupBox(tr("Operating"), content);
    auto *opForm  = new QFormLayout(opGroup);

    auto *hbSpin = new QSpinBox(opGroup);
    hbSpin->setRange(1, 60);
    hbSpin->setValue(m_config.heartbeatIntervalPeriods);
    hbSpin->setSuffix(tr(" periods"));
    opForm->addRow(tr("Heartbeat interval:"), hbSpin);

    auto *autoReplyCheck = new QCheckBox(tr("Auto-reply to queries"), opGroup);
    autoReplyCheck->setChecked(m_config.autoReply);
    opForm->addRow(QString(), autoReplyCheck);

    // Distance units
    auto *distCombo = new QComboBox(opGroup);
    distCombo->addItem(tr("Miles"),      true);
    distCombo->addItem(tr("Kilometers"), false);
    distCombo->setCurrentIndex(m_config.distMiles ? 0 : 1);
    opForm->addRow(tr("Distance units:"), distCombo);

    // Auto-ATU
    auto *autoAtuCheck = new QCheckBox(tr("Auto-ATU on band change"), opGroup);
    autoAtuCheck->setChecked(m_config.autoAtu);
    opForm->addRow(QString(), autoAtuCheck);

    vbox->addWidget(opGroup);

    // ── WebSocket API ────────────────────────────────────────────────────────
    auto *wsGroup = new QGroupBox(tr("WebSocket API"), content);
    auto *wsForm  = new QFormLayout(wsGroup);

    auto *wsEnabledCheck = new QCheckBox(tr("Enable WebSocket API"), wsGroup);
    wsEnabledCheck->setChecked(m_config.wsEnabled);
    wsForm->addRow(QString(), wsEnabledCheck);

    auto *wsHostCombo = new QComboBox(wsGroup);
    wsHostCombo->addItem(tr("Localhost only (127.0.0.1)"), QStringLiteral("127.0.0.1"));
    wsHostCombo->addItem(tr("All interfaces (0.0.0.0)"),   QStringLiteral("0.0.0.0"));
    wsHostCombo->setCurrentIndex(
        m_config.wsHost == QStringLiteral("0.0.0.0") ? 1 : 0);
    wsForm->addRow(tr("Listen address:"), wsHostCombo);

    auto *wsPortSpin = new QSpinBox(wsGroup);
    wsPortSpin->setRange(1024, 65535);
    wsPortSpin->setValue(m_config.wsPort);
    wsForm->addRow(tr("Port:"), wsPortSpin);

    auto *wsRestartNote = new QLabel(
        tr("<i>Changes to address or port require a restart to take effect.</i>"),
        wsGroup);
    wsRestartNote->setWordWrap(true);
    wsRestartNote->setStyleSheet(QStringLiteral("color: #c9a84c;"));
    wsRestartNote->hide();
    wsForm->addRow(wsRestartNote);

    // Show restart note whenever address or port is changed
    auto showRestartNote = [wsRestartNote]() { wsRestartNote->show(); };
    connect(wsHostCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            wsGroup, [=](int) { showRestartNote(); });
    connect(wsPortSpin, &QSpinBox::valueChanged,
            wsGroup, [=](int) { showRestartNote(); });

    vbox->addWidget(wsGroup);

    // ── Reporting ────────────────────────────────────────────────────────────
    auto *reportGroup = new QGroupBox(tr("Reporting"), content);
    auto *reportForm  = new QFormLayout(reportGroup);

    auto *pskCheck = new QCheckBox(tr("Submit spots to PSK Reporter (pskreporter.info)"), reportGroup);
    pskCheck->setChecked(m_config.pskReporterEnabled);
    reportForm->addRow(QString(), pskCheck);

    auto *pskNote = new QLabel(
        tr("<i>Spots are batched and sent every ~2 minutes. "
           "Requires callsign and grid to be set.</i>"),
        reportGroup);
    pskNote->setWordWrap(true);
    pskNote->setStyleSheet(QStringLiteral("color: #c9a84c;"));
    reportForm->addRow(pskNote);

    vbox->addWidget(reportGroup);
    vbox->addStretch(1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outerVbox->addWidget(buttons);
    outerVbox->setContentsMargins(8, 8, 8, 8);
    outerVbox->setSpacing(4);

    dlg.resize(500, 620);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // Resolve audio device names from combo selections
    auto devName = [](QComboBox *cb) -> QString {
        const QVariant d = cb->currentData();
        if (d.isValid() && !d.toString().isEmpty()) return d.toString();
        const QString t = cb->currentText().trimmed();
        return (t == cb->itemText(0)) ? QString() : t;  // item 0 = "(default)"
    };
    const QString newInDev  = devName(inDevCombo);
    const QString newOutDev = devName(outDevCombo);

    const int  newModemType = modemTypeCbo->currentData().toInt();
    const int  newSubmode   = modemSpeedCbo->currentIndex();
    const bool modemChanged = (newModemType != m_config.modemType);
    const bool submodeChanged = (!modemChanged && newSubmode != m_config.submode);

    const bool audioChanged =
        (m_config.audioInputName  != newInDev ||
         m_config.audioOutputName != newOutDev);

    m_config.callsign                  = callEdit->text().trimmed().toUpper();
    m_config.grid                      = gridEdit->text().trimmed().toUpper();
    m_config.stationInfo               = infoEdit->text().trimmed();
    m_config.stationStatus             = statusEdit->text().trimmed();
    m_config.cqMessage                 = cqEdit->text().trimmed();
    m_config.audioInputName            = newInDev;
    m_config.audioOutputName           = newOutDev;
    m_config.heartbeatIntervalPeriods  = hbSpin->value();
    m_config.autoReply                 = autoReplyCheck->isChecked();
    m_config.distMiles                 = distCombo->currentData().toBool();
    m_config.autoAtu                   = autoAtuCheck->isChecked();
    m_config.wsEnabled                 = wsEnabledCheck->isChecked();
    m_config.wsHost                    = wsHostCombo->currentData().toString();
    m_config.wsPort                    = wsPortSpin->value();
    m_config.pskReporterEnabled        = pskCheck->isChecked();
    m_config.save();

    // Update callsign/grid display label
    if (m_callGridLabel) {
        const QString cs = m_config.callsign.isEmpty() ? tr("(no call)") : m_config.callsign;
        const QString gr = m_config.grid.isEmpty()     ? tr("?")         : m_config.grid;
        m_callGridLabel->setText(QStringLiteral(" %1 / %2 ").arg(cs, gr));
    }

    // Update distance unit in model
    m_model->setDistanceMiles(m_config.distMiles);

    // Update PSK Reporter local station (callsign/grid may have changed)
    if (m_pskReporter) {
        if (m_config.pskReporterEnabled)
            m_pskReporter->setLocalStation(m_config.callsign, m_config.grid,
                                            QStringLiteral("JF8Call/%1").arg(
                                                QString::fromLatin1(JF8CALL_VERSION_STR)));
    }

    // Apply modem type change (also restarts audio)
    if (modemChanged) {
        apiSetModem(newModemType);
    } else {
        if (submodeChanged)
            apiSetSubmode(newSubmode);
        if (audioChanged) { stopAudio(); startAudio(); }
        if (m_wsServer) m_wsServer->pushConfigChanged();
    }
}

void MainWindow::onWaterfallFreqClicked(float hz)
{
    m_config.txFreqHz = hz;
    m_txFreqSpin->setValue(hz);
    m_waterfall->setTxFreqHz(hz);
    m_config.save();
}

void MainWindow::onRadioConnectionChanged(bool connected)
{
    if (connected) {
        m_radioStatusLabel->setText(QStringLiteral("Radio: connected"));
        m_radioPollTimer->start();
        if (m_wsServer) m_wsServer->pushRadioConnected(m_radioFreqKhz, m_radioMode);
    } else {
        m_radioStatusLabel->setText(QStringLiteral("Radio: not connected"));
        m_radioPollTimer->stop();
        if (m_wsServer) m_wsServer->pushRadioDisconnected();
    }
}

void MainWindow::onRadioPollResult(double khz, const QString &mode)
{
    m_radioFreqKhz = khz;
    m_radioMode    = mode;
    m_radioStatusLabel->setText(
        QStringLiteral("Radio: %1 kHz / %2").arg(khz, 0, 'f', 3).arg(mode));
}

void MainWindow::onRadioError(const QString &msg)
{
    statusBar()->showMessage(QStringLiteral("Radio: ") + msg, 5000);
}

void MainWindow::onRadioPollTimer()
{
    QMetaObject::invokeMethod(m_hamlib, &HamlibController::requestPoll,
                              Qt::QueuedConnection);
}

// ── Public API (WsServer interface) ──────────────────────────────────────────

double MainWindow::apiRadioFreqKhz() const { return m_radioFreqKhz; }
QString MainWindow::apiRadioMode()   const { return m_radioMode; }

QJsonArray MainWindow::apiGetMessages(int offset, int limit) const
{
    QJsonArray arr;
    const int total = m_model->messageCount();
    for (int i = offset; i < qMin(offset + limit, total); ++i) {
        const JS8Message &msg = m_model->messageAt(i);
        QJsonObject o;
        o[QStringLiteral("time")]     = msg.utc.toString(QStringLiteral("HH:mm:ss"));
        o[QStringLiteral("utc_iso")]  = msg.utc.toString(Qt::ISODate);
        o[QStringLiteral("freq_hz")]  = static_cast<double>(msg.audioFreqHz);
        o[QStringLiteral("snr_db")]   = msg.snrDb;
        o[QStringLiteral("submode")]  = msg.submodeStr;
        o[QStringLiteral("from")]     = msg.from;
        o[QStringLiteral("to")]       = msg.to;
        o[QStringLiteral("body")]     = msg.body;
        o[QStringLiteral("raw")]      = msg.rawText;
        arr.append(o);
    }
    return arr;
}

void MainWindow::apiSetCallsign(const QString &s)
{
    m_config.callsign = s.trimmed().toUpper();
    if (m_callGridLabel) {
        const QString cs = m_config.callsign.isEmpty() ? tr("(no call)") : m_config.callsign;
        const QString gr = m_config.grid.isEmpty()     ? tr("?")         : m_config.grid;
        m_callGridLabel->setText(QStringLiteral(" %1 / %2 ").arg(cs, gr));
    }
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
    if (m_config.pskReporterEnabled && m_pskReporter)
        m_pskReporter->setLocalStation(m_config.callsign, m_config.grid,
                                        QStringLiteral("JF8Call/%1").arg(
                                            QString::fromLatin1(JF8CALL_VERSION_STR)));
}

void MainWindow::apiSetGrid(const QString &s)
{
    m_config.grid = s.trimmed().toUpper();
    if (m_callGridLabel) {
        const QString cs = m_config.callsign.isEmpty() ? tr("(no call)") : m_config.callsign;
        const QString gr = m_config.grid.isEmpty()     ? tr("?")         : m_config.grid;
        m_callGridLabel->setText(QStringLiteral(" %1 / %2 ").arg(cs, gr));
    }
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
    if (m_config.pskReporterEnabled && m_pskReporter)
        m_pskReporter->setLocalStation(m_config.callsign, m_config.grid,
                                        QStringLiteral("JF8Call/%1").arg(
                                            QString::fromLatin1(JF8CALL_VERSION_STR)));
}

void MainWindow::apiSetSubmode(int idx)
{
    if (idx < 0 || idx >= m_modem->submodeCount()) return;
    m_config.submode = idx;
    { QSignalBlocker b(m_submodeCbo); m_submodeCbo->setCurrentIndex(idx); }
    const int period = m_modem->submodeParms(idx).periodSeconds;
    if (period > 0)
        m_periodClock->setPeriodSeconds(period);
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiSetFrequencyKhz(double khz)
{
    m_config.frequencyKhz = khz;
    { QSignalBlocker b(m_freqSpin); m_freqSpin->setValue(khz); }
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiSetTxFreqHz(double hz)
{
    m_config.txFreqHz = hz;
    { QSignalBlocker b(m_txFreqSpin); m_txFreqSpin->setValue(hz); }
    if (m_waterfall) m_waterfall->setTxFreqHz(static_cast<float>(hz));
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiSetHeartbeatEnabled(bool v)
{
    m_config.heartbeatEnabled = v;
    { QSignalBlocker b(m_hbCheck); m_hbCheck->setChecked(v); }
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiSetHeartbeatInterval(int periods)
{
    if (periods < 1) return;
    m_config.heartbeatIntervalPeriods = periods;
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiSetAutoReply(bool v)
{
    m_config.autoReply = v;
    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiSetStationInfo(const QString &s)
{
    m_config.stationInfo = s;
    m_config.save();
}

void MainWindow::apiSetStationStatus(const QString &s)
{
    m_config.stationStatus = s;
    m_config.save();
}

void MainWindow::apiSetCqMessage(const QString &s)
{
    m_config.cqMessage = s;
    m_config.save();
}

void MainWindow::apiSetDistMiles(bool miles)
{
    m_config.distMiles = miles;
    m_model->setDistanceMiles(miles);
    m_config.save();
}

void MainWindow::apiSetAutoAtu(bool v)
{
    m_config.autoAtu = v;
    m_config.save();
}

void MainWindow::apiSetPskReporterEnabled(bool v)
{
    m_config.pskReporterEnabled = v;
    m_config.save();
    if (m_pskReporter && v)
        m_pskReporter->setLocalStation(m_config.callsign, m_config.grid,
            QStringLiteral("JF8Call/%1").arg(QString::fromLatin1(JF8CALL_VERSION_STR)));
}

void MainWindow::apiSetAudioInput(const QString &name)
{
    m_config.audioInputName = name;
    m_config.save();
    stopAudio();
    startAudio();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiSetAudioOutput(const QString &name)
{
    m_config.audioOutputName = name;
    m_config.save();
    stopAudio();
    startAudio();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiSetRigConfig(const RigConfig &cfg)
{
    rigConfigToConfig(cfg);
    m_config.save();
}

void MainWindow::apiQueueTx(const QString &text)
{
    transmitMessage(text);
}

void MainWindow::apiQueueTxSubmode(const QString &text, int submode)
{
    if (m_config.callsign.isEmpty()) return;
    auto frames = m_modem->pack(m_config.callsign.toStdString(),
                                m_config.grid.toStdString(),
                                text.toStdString(), submode);
    for (const ModemTxFrame &f : frames) {
        QVariantMap m;
        m[QStringLiteral("payload")]   = QString::fromStdString(f.payload);
        m[QStringLiteral("frameType")] = f.frameType;
        m[QStringLiteral("submode")]   = f.submode;
        m_pendingTxFrames.append(m);
    }
    if (m_wsServer) m_wsServer->pushTxQueued(m_pendingTxFrames.size());
}

void MainWindow::apiClearTxQueue()
{
    m_pendingTxFrames.clear();
}

void MainWindow::apiRestartAudio()
{
    stopAudio();
    startAudio();
}

void MainWindow::apiConnectRadio(const RigConfig &cfg)
{
    rigConfigToConfig(cfg);
    m_config.save();
    QMetaObject::invokeMethod(m_hamlib, &HamlibController::disconnectRig, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_hamlib, [this, cfg]() {
        m_hamlib->connectRig(cfg);
    }, Qt::QueuedConnection);
}

void MainWindow::apiDisconnectRadio()
{
    QMetaObject::invokeMethod(m_hamlib, &HamlibController::disconnectRig, Qt::QueuedConnection);
}

bool MainWindow::apiSetFrequency(double khz)
{
    bool ok = false;
    QMetaObject::invokeMethod(m_hamlib, [this, khz, &ok]() {
        ok = m_hamlib->setFrequency(khz);
    }, Qt::BlockingQueuedConnection);
    if (ok) {
        m_config.frequencyKhz = khz;
        { QSignalBlocker b(m_freqSpin); m_freqSpin->setValue(khz); }
        m_config.save();
        if (m_config.autoAtu)
            QMetaObject::invokeMethod(m_hamlib, &HamlibController::startTune,
                                      Qt::QueuedConnection);
    }
    return ok;
}

void MainWindow::apiTuneRadio()
{
    if (m_hamlib->isConnected())
        QMetaObject::invokeMethod(m_hamlib, &HamlibController::startTune,
                                  Qt::QueuedConnection);
}

bool MainWindow::apiSetPtt(bool on)
{
    QMetaObject::invokeMethod(m_hamlib, [this, on]() {
        m_hamlib->setPtt(on);
    }, Qt::QueuedConnection);
    return true;
}

void MainWindow::apiSetModem(int type)
{
    // Reject unsupported types at compile time
    if (type == 1) {
#ifndef HAVE_CODEC2
        return;
#endif
    } else if (type == 2) {
#ifndef HAVE_OLIVIA
        return;
#endif
    } else if (type == 3) {
#ifndef HAVE_PSK_MODEM
        return;
#endif
    } else if (type != 0) {
        return;
    }
    if (type == m_config.modemType) return;

    m_config.modemType = type;
    // Reset to a sensible default submode for each modem
    m_config.submode   = (type == 3) ? 1 : 0;  // PSK defaults to BPSK63 (idx 1)

#ifdef HAVE_CODEC2
    if (type == 1)
        m_modem = std::make_unique<Codec2Modem>(0);
    else
#endif
#ifdef HAVE_OLIVIA
    if (type == 2)
        m_modem = std::make_unique<OliviaModem>(0);
    else
#endif
#ifdef HAVE_PSK_MODEM
    if (type == 3)
        m_modem = std::make_unique<PskModem>(m_config.submode);
    else
#endif
        m_modem = std::make_unique<Gfsk8Modem>();

    restartDecodeWorker();
    refreshSubmodeCombo();

    const bool streaming = (m_modem->submodeParms(0).periodSeconds == 0);
    if (streaming) {
        m_periodClock->stop();
        disconnect(m_audioChunkConn);
        m_audioChunkConn = connect(
            m_audioIn, &AudioInput::audioChunkReady,
            this, &MainWindow::onAudioChunkReady, Qt::QueuedConnection);
        m_streamTxTimer->start();
    } else {
        m_streamTxTimer->stop();
        disconnect(m_audioChunkConn);
        m_periodClock->setPeriodSeconds(m_modem->submodeParms(0).periodSeconds);
        m_periodClock->start();
    }
    stopAudio();
    startAudio();

    m_config.save();
    if (m_wsServer) m_wsServer->pushConfigChanged();
}

void MainWindow::apiClearMessages()
{
    m_model->clear();
    m_decodeCount = 0;
    m_countLabel->setText(QStringLiteral("0 decoded"));
}

#include "mainwindow.moc"
