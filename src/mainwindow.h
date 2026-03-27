#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QMainWindow>
#include <QLabel>
#include <QFrame>
#include <QTableView>
#include <QSplitter>
#include <QByteArray>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTimer>
#include <QThread>
#include <QCheckBox>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QHash>
#include <memory>
#include <vector>
#include "config.h"
#include "hamlibcontroller.h"
#include "updatechecker.h"
#include "imodem.h"
#include "jf8message.h"
#include "solardata.h"
#include "freqschedule.h"

class MessageModel;
class WaterfallWidget;
class AudioInput;
class AudioOutput;
class PeriodClock;
class WsServer;
class PskReporter;
class RelayServer;
class AprsClient;
class QsoLog;
class QSlider;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // ── Public API (used by WsServer) ─────────────────────────────────────
    // All methods are called on the main thread; safe to call directly.

    // Read-only state
    const Config        &apiConfig()             const { return m_config; }
    bool                 apiIsTransmitting()     const { return m_transmitting; }
    bool                 apiIsAudioRunning()     const { return m_audioStarted; }
    bool                 apiIsRadioConnected()   const { return m_hamlib->isConnected(); }
    int                  apiTxQueueSize()        const { return m_pendingTxFrames.size(); }
    double               apiRadioFreqKhz()       const;
    QString              apiRadioMode()          const;
    const QList<QVariantMap> &apiTxQueue()       const { return m_pendingTxFrames; }
    QJsonArray           apiGetMessages(int offset = 0, int limit = 100) const;
    std::vector<float>   apiLatestSpectrum()     const { return m_latestSpectrum; }
    float                apiSpectrumSampleRate() const { return m_latestSpectrumRate; }

    // Mutations — update config, save, reflect in UI
    void apiSetCallsign(const QString &s);
    void apiSetGrid(const QString &s);
    void apiSetSubmode(int idx);          // 0-4 UI index
    void apiSetFrequencyKhz(double khz);
    void apiSetTxFreqHz(double hz);
    void apiSetHeartbeatEnabled(bool v);
    void apiSetHeartbeatIntervalMins(int mins);
    void apiSetHeartbeatSubChannel(bool v);
    void apiSetTxEnabled(bool v);
    void apiSetAutoReply(bool v);
    void apiSetStationInfo(const QString &s);
    void apiSetStationStatus(const QString &s);
    void apiSetCqMessage(const QString &s);
    void apiSetDistMiles(bool miles);
    void apiSetAutoAtu(bool v);
    void apiSetPskReporterEnabled(bool v);
    void apiSetInfoMaxAgeMins(int mins);
    void apiSetHeardMaxAgeMins(int mins);
    void apiSetAudioInput(const QString &name);
    void apiSetAudioOutput(const QString &name);
    void apiSetRigConfig(const RigConfig &cfg);

    // Actions
    void apiQueueTx(const QString &text);
    void apiQueueTxSubmode(const QString &text, int submode);
    void apiClearTxQueue();
    void apiSetModem(int type);         // 0=Gfsk8, 1=Codec2
    void apiRestartAudio();
    void apiConnectRadio(const RigConfig &cfg);
    void apiDisconnectRadio();
    bool apiSetFrequency(double khz);
    bool apiSetPtt(bool on);
    void apiTuneRadio();
    int  apiGetRfPower(QString *err = nullptr)     const;
    bool apiSetRfPower(int pct, QString *err = nullptr);
    int  apiGetAfVolume(QString *err = nullptr)    const;
    bool apiSetAfVolume(int pct, QString *err = nullptr);
    int  apiGetMute(QString *err = nullptr)        const;
    bool apiSetMute(bool muted, QString *err = nullptr);
    void apiClearMessages();

    // QSO logging
    void apiLogQso(const QString &callsign, const QString &grid,
                   int snrDb, const QString &notes = QString());
    QJsonArray apiGetQsoLog(int offset = 0, int limit = 100) const;
    QString apiExportAdif() const;

    // Solar data
    QJsonObject apiGetSolarData() const;

    // Frequency schedule
    void apiSetFreqSchedule(const QList<FreqScheduleEntry> &schedule);
    QJsonArray apiGetFreqSchedule() const;

    // Band list
    void apiSetBandList(const QList<BandEntry> &bands);
    QJsonArray apiGetBandList() const;

    // Group memberships
    void apiSetGroups(const QStringList &groups);
    QStringList apiGetGroups() const;

protected:
    void closeEvent(QCloseEvent *) override;
    void showEvent(QShowEvent *) override;

private slots:
    // Audio / modem
    void onSpectrumReady(std::vector<float> magnitudes, float sampleRateHz);
    void onPeriodStarted(int utc);
    void onAudioChunkReady(QByteArray chunk);  // streaming modem RX
    void onStreamTxTimer();                    // streaming modem TX polling
    void onDecodeFinished(const QList<QVariantMap> &results);
    void onPlaybackFinished();

    // Radio
    void onRadioConnectionChanged(bool connected);
    void onRadioPollResult(double khz, const QString &mode);
    void onRadioError(const QString &msg);

    // UI actions
    void onSendClicked();
    void onHbClicked();
    void onSnrQueryClicked();
    void onInfoQueryClicked();
    void onGridQueryClicked();
    void onStatusQueryClicked();
    void onHearingQueryClicked();
    void onCqClicked();
    void onHaltClicked();
    void onInfoTableClicked(const QModelIndex &index);
    void onDeselectClicked();
    void onAllClicked();
    void onMsgBtnClicked();
    void onOpenInbox();
    void updateInboxNotification();
    void onRadioSetup();
    void onPreferences();
    void onWaterfallFreqClicked(float hz);

    // Periodic
    void onRadioPollTimer();
    void onHeartbeatCheck();
    void startupChecks();
    void onFrameCleanupTimer();
    void onFreqScheduleCheck();
    void onHeardAgeTimer();

    // Helpers
    void rebuildHeardPane();

    // Solar data
    void onSolarDataUpdated(const SolarData &data);

    // Band selector
    void onBandSelected(int index);

    // Schedule / QSO UI
    void onFreqScheduleEdit();
    void onQsoLogOpen();
    void onQsoLogAdifExport();

    // Band list editor
    void onBandListEdit();

    // Clock tick
    void onClockTick();

    // Auto-reply
    void sendAutoReply(const QString &toCall, const QString &body, int snrDb);

signals:
    // Sent to DecodeWorker on its thread
    void requestDecode(QByteArray samples12k, int utc, int submodes);

    // TUI / external observer signals
    void messageDecoded(const JF8Message &msg);
    void spectrumReady(std::vector<float> bins, float sampleRate);
    void txStarted();
    void txFinished();
    void radioStatusChanged();

private:
    void setupUi();
    void setupMenuBar();
    void setupToolBar();
    void setupCentralWidget();
    void setupStatusBar();
    void applyStyleSheet();
    void populateBandCombo();
    void startAudio();
    void saveInfoPane();
    void saveHeardPane();
    void loadInfoPane();
    void loadHeardPane();
    void stopAudio();
    void transmitMessage(const QString &text, const QString &gridOverride = QString());
    void sendHeartbeat();
    void transmitNextFrame();          // shared TX path for period-based and streaming
    void refreshSubmodeCombo();        // repopulate m_submodeCbo from active modem
    void restartDecodeWorker();        // recreate decode thread+worker for active modem type
    void setTransmitting(bool tx);
    RigConfig configToRigConfig() const;
    void rigConfigToConfig(const RigConfig &cfg);

    Config m_config;

    // Active modem (JS8 by default; swappable for future modems)
    std::unique_ptr<IModem> m_modem;

    // WebSocket API server
    WsServer *m_wsServer = nullptr;

    // Latest spectrum (shared with WsServer for pushes)
    std::vector<float> m_latestSpectrum;
    float              m_latestSpectrumRate = 12000.0f;

    // Models
    MessageModel *m_model;

    // Audio
    AudioInput  *m_audioIn;
    AudioOutput *m_audioOut;
    QThread     *m_decodeThread = nullptr;
    int          m_txFrameIndex = 0;
    QList<QVariantMap> m_pendingTxFrames;  // {payload, frameType, submode}
    bool         m_transmitting = false;
    bool         m_audioStarted = false;

    // Period clock (used only for period-based modems)
    PeriodClock *m_periodClock;
    QMetaObject::Connection m_audioChunkConn; // audioChunkReady → onAudioChunkReady
    QTimer *m_streamTxTimer = nullptr;        // TX polling for streaming modems

    // Hamlib
    HamlibController *m_hamlib;
    QThread          *m_hamlibThread  = nullptr;
    QTimer           *m_radioPollTimer      = nullptr;
    QTimer           *m_radioReconnectTimer = nullptr;
    double            m_radioFreqKhz  = 0.0;
    QString           m_radioMode;

    // Heartbeat
    QTimer *m_heartbeatTimer     = nullptr;
    QTimer *m_wsStatusTimer      = nullptr;
    int     m_periodCount        = 0;
    bool    m_splitterRestored   = false;

    // UI widgets
    WaterfallWidget *m_waterfall          = nullptr;
    QTableView      *m_messageTable       = nullptr;   // right "Info" pane
    QPlainTextEdit  *m_infoPane           = nullptr;   // left "Heard" text log
    QPlainTextEdit  *m_interactiveDisplay = nullptr;
    QSplitter       *m_vSplit             = nullptr;
    QSplitter       *m_hSplit             = nullptr;

    // Toolbar widgets
    QLabel         *m_callGridLabel = nullptr;  // read-only "CALL / GRID" display
    QDoubleSpinBox *m_freqSpin;
    QComboBox      *m_submodeCbo;
    QDoubleSpinBox *m_txFreqSpin;
    QPushButton    *m_connectBtn;
    QCheckBox      *m_hbCheck;
    QCheckBox      *m_txCheck     = nullptr;   // master TX enable
    QLabel         *m_hbCountdown = nullptr;   // HB timer countdown label
    int             m_hbSecsRemaining = 0;     // seconds until next HB transmission

    // TX input area
    QLineEdit   *m_txEdit;
    QPushButton *m_sendBtn;
    QPushButton *m_hbBtn;
    QPushButton *m_snrBtn;
    QPushButton *m_infoBtn;
    QPushButton *m_gridBtn      = nullptr;
    QPushButton *m_statusBtn    = nullptr;
    QPushButton *m_hearingBtn   = nullptr;
    QPushButton *m_cqBtn        = nullptr;
    QPushButton *m_haltBtn      = nullptr;
    QPushButton *m_deselectBtn  = nullptr;
    QPushButton *m_allBtn       = nullptr;
    QPushButton *m_msgBtn       = nullptr;
    QPushButton *m_inboxNotifyBtn = nullptr;

    // Focus mode — callsign selected in Info table ("" = none, "@ALL" = broadcast)
    QString m_selectedCallsign;

    // PSK Reporter
    PskReporter  *m_pskReporter    = nullptr;

    // Update checker
    UpdateChecker *m_updateChecker = nullptr;

    // Update notification bar (shown when a newer version is detected)
    QFrame *m_updateBar = nullptr;

    // Solar data
    SolarDataFetcher *m_solarFetcher = nullptr;
    SolarData         m_solarData;
    QLabel           *m_solarLabel   = nullptr;

    // Relay server
    RelayServer *m_relayServer = nullptr;

    // APRS-IS client
    AprsClient  *m_aprsClient  = nullptr;

    // Band combo (toolbar)
    QComboBox *m_bandCombo = nullptr;

    // Clock labels (top-right of toolbar)
    QLabel    *m_utcClockLabel = nullptr;
    QLabel    *m_lclClockLabel = nullptr;
    QTimer    *m_clockTimer    = nullptr;

    // Waterfall controls
    QSlider   *m_gainSlider      = nullptr;
    QComboBox *m_wfModeCbo       = nullptr;

    // Frequency schedule timer
    QTimer    *m_freqScheduleTimer = nullptr;
    int        m_lastScheduledHhmm = -1;  // to avoid double-firing

    // Status bar
    QLabel *m_radioStatusLabel;
    QLabel *m_rxTxLabel;
    QLabel *m_countLabel;
    int     m_decodeCount = 0;
    struct HeardEntry { QString text; QDateTime lastUpdate; };
    QHash<int, HeardEntry>  m_heardFreqBlock;  // rounded freq (Hz/10) → entry
    QTimer                 *m_heardAgeTimer = nullptr;
    QHash<QString, qint64> m_recentDecodes;     // msgKey → epoch_sec, cross-period dedup
    QHash<QString, qint64> m_lastRelayNotify;  // callsign → epoch_sec, throttle relay pings

    // GFSK8 multi-frame assembly buffer.
    // Keyed by round(freqHz/10); holds accumulated rawText across frame periods.
    struct GfskFrameBuffer {
        QString assembledRawText; ///< rawText from first + middle frames
        qint64  firstSeenSec;     ///< epoch seconds when first frame arrived
        float   freqHz;           ///< audio frequency (for re-creating ModemDecoded)
        int     snrDb;            ///< SNR from first frame
        int     submode;
    };
    QHash<int, GfskFrameBuffer> m_gfsk8FrameBuffers;
    QTimer *m_frameCleanupTimer = nullptr;
};
