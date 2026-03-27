// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call — JS8Call-compatible application
// Copyright (C) 2026 Ordo Artificum LLC

#include "tuiwindow.h"
#include "mainwindow.h"
#include "jf8call_version.h"

#include <QCoreApplication>
#include <algorithm>
#include <cstdio>
#include <unistd.h>

#ifdef HAVE_NCURSES
#include <ncurses.h>
#endif

// ── ncurses color pair indices ───────────────────────────────────────────────
#ifdef HAVE_NCURSES
enum {
    PAIR_DEFAULT   = 1,   // default text on terminal background
    PAIR_HEADER    = 2,   // bold yellow on blue — title bar + column headers
    PAIR_MSG       = 3,   // green — received messages
    PAIR_MSG_ME    = 4,   // bold yellow — messages addressed to my callsign
    PAIR_WATERFALL = 5,   // cyan — ASCII spectrum waterfall
    PAIR_DIM       = 6,   // dim white — separators and key hints
    PAIR_INPUT     = 7,   // bold white — input line
    PAIR_STATUS    = 8,   // bold yellow — temporary status messages
};
#endif

// ── Spectrum character set (6 levels: silence → strong signal) ───────────────
static const char kSpecChars[] = " .:+#@";

#ifdef HAVE_NCURSES
static int dbToLevel(float db)
{
    // Map -90 dBFS → level 0, -30 dBFS → level 5
    const float norm = (db + 90.0f) / 60.0f;
    return std::max(0, std::min(5, static_cast<int>(norm * 6.0f)));
}
#endif

// ── Constructor / Destructor ─────────────────────────────────────────────────

TuiWindow::TuiWindow(MainWindow *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_redrawTimer = new QTimer(this);
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);

    connect(m_redrawTimer, &QTimer::timeout, this, &TuiWindow::redraw);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        m_statusMsg.clear();
        m_dirty = true;
    });

    connect(m_app, &MainWindow::messageDecoded,
            this, &TuiWindow::onMessageDecoded);
    connect(m_app, &MainWindow::spectrumReady,
            this, &TuiWindow::onSpectrumReady);
    connect(m_app, &MainWindow::txStarted,
            this, &TuiWindow::onTxStarted);
    connect(m_app, &MainWindow::txFinished,
            this, &TuiWindow::onTxFinished);
    connect(m_app, &MainWindow::radioStatusChanged,
            this, &TuiWindow::onRadioStatusChanged);
}

TuiWindow::~TuiWindow()
{
#ifdef HAVE_NCURSES
    teardownNcurses();
#endif
}

// ── start() ──────────────────────────────────────────────────────────────────

void TuiWindow::start()
{
#ifdef HAVE_NCURSES
    setupNcurses();

    m_stdinNotifier = new QSocketNotifier(STDIN_FILENO,
                                          QSocketNotifier::Read, this);
    connect(m_stdinNotifier, &QSocketNotifier::activated,
            this, &TuiWindow::onStdinReady);

    m_redrawTimer->start(250);   // 4 fps
    m_dirty = true;
#else
    fprintf(stderr, "jf8call: TUI mode requires ncurses (not compiled in)\n");
    QCoreApplication::quit();
#endif
}

// ── Event slots (always defined — update data, mark dirty) ───────────────────

void TuiWindow::onMessageDecoded(const JF8Message &msg)
{
    MsgEntry e;
    e.utc    = msg.utc;
    e.freqHz = msg.audioFreqHz;
    e.snrDb  = msg.snrDb;
    e.from   = msg.from;
    e.to     = msg.to;
    e.body   = msg.body;   // empty for heartbeats/queries — TO column shows the command

    m_messages.append(e);
    while (m_messages.size() > kMaxMessages)
        m_messages.removeFirst();

    m_dirty = true;
}

void TuiWindow::onSpectrumReady(std::vector<float> bins, float sampleRate)
{
    m_waterfall.append({std::move(bins), sampleRate});
    while (m_waterfall.size() > kWfRows)
        m_waterfall.removeFirst();

    m_dirty = true;
}

void TuiWindow::onTxStarted()          { m_txActive = true;  m_dirty = true; }
void TuiWindow::onTxFinished()         { m_txActive = false; m_dirty = true; }
void TuiWindow::onRadioStatusChanged() { m_dirty = true; }

void TuiWindow::setStatus(const QString &msg, int durationMs)
{
    m_statusMsg = msg;
    m_statusTimer->start(durationMs);
    m_dirty = true;
}

// ── ncurses implementation ────────────────────────────────────────────────────
#ifdef HAVE_NCURSES

int TuiWindow::inputRow()  const { return LINES - 2; }
int TuiWindow::hintsRow()  const { return LINES - 1; }
int TuiWindow::msgBottom() const { return LINES - 3; }
int TuiWindow::msgHeight() const { return std::max(0, msgBottom() - kMsgTop); }

// ---------------------------------------------------------------------------
// ncurses lifecycle
// ---------------------------------------------------------------------------

void TuiWindow::setupNcurses()
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(PAIR_DEFAULT,   COLOR_WHITE,  -1);
        init_pair(PAIR_HEADER,    COLOR_YELLOW, COLOR_BLUE);
        init_pair(PAIR_MSG,       COLOR_GREEN,  -1);
        init_pair(PAIR_MSG_ME,    COLOR_YELLOW, -1);
        init_pair(PAIR_WATERFALL, COLOR_CYAN,   -1);
        init_pair(PAIR_DIM,       COLOR_WHITE,  -1);
        init_pair(PAIR_INPUT,     COLOR_WHITE,  -1);
        init_pair(PAIR_STATUS,    COLOR_YELLOW, -1);
    }

    m_ncursesActive = true;
}

void TuiWindow::teardownNcurses()
{
    if (!m_ncursesActive) return;
    m_ncursesActive = false;
    endwin();
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void TuiWindow::drawSeparator(int row)
{
    if (row < 0 || row >= LINES) return;
    attron(COLOR_PAIR(PAIR_DIM) | A_DIM);
    mvhline(row, 0, ACS_HLINE, COLS);
    attroff(COLOR_PAIR(PAIR_DIM) | A_DIM);
}

void TuiWindow::drawTitleBar()
{
    const Config &cfg = m_app->apiConfig();

    // Submode name
    const char *subStr = "?";
    if (cfg.modemType == 0) {
        static const char *const kGfsk[] = {"Normal","Fast","Turbo","Slow","Ultra"};
        if (cfg.submode >= 0 && cfg.submode < 5) subStr = kGfsk[cfg.submode];
    } else if (cfg.modemType == 1) {
        static const char *const kC2[] = {"DATAC0","DATAC1","DATAC3"};
        if (cfg.submode >= 0 && cfg.submode < 3) subStr = kC2[cfg.submode];
    }

    const QString call = cfg.callsign.isEmpty()
                         ? QStringLiteral("(none)") : cfg.callsign;
    const QString grid = cfg.grid.isEmpty()
                         ? QStringLiteral("------") : cfg.grid;

    // Radio info (only if connected)
    QString radioStr;
    if (m_app->apiIsRadioConnected())
        radioStr = QString("  Radio:%1 kHz").arg(m_app->apiRadioFreqKhz(), 0, 'f', 1);

    const QString title = QString("  JF8Call %1  \u2502  %2 %3"
                                  "  \u2502  %4 kHz %5"
                                  "  \u2502  [%6]%7")
                          .arg(QLatin1String(JF8CALL_VERSION_STR))
                          .arg(call, grid)
                          .arg(cfg.frequencyKhz, 0, 'f', 1)
                          .arg(QLatin1String(subStr))
                          .arg(m_txActive ? QStringLiteral("** TX **")
                                          : QStringLiteral("  IDLE  "))
                          .arg(radioStr);

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvhline(kTitleRow, 0, ' ', COLS);
    mvprintw(kTitleRow, 0, "%.*s", COLS, title.toLocal8Bit().constData());
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
}

void TuiWindow::drawWaterfall()
{
    const int width  = std::max(1, COLS - 2);   // inside the | borders
    const float loHz = 200.0f, hiHz = 3000.0f;

    for (int r = 0; r < kWfRows; r++) {
        const int screenRow = kWfTop + r;
        if (screenRow >= LINES) break;

        attron(COLOR_PAIR(PAIR_WATERFALL));
        mvaddch(screenRow, 0, '|');

        const int srcIdx = m_waterfall.size() - kWfRows + r;
        if (srcIdx >= 0 && srcIdx < m_waterfall.size()) {
            const auto &row  = m_waterfall[srcIdx];
            const auto &bins = row.bins;
            if (!bins.empty() && row.sampleRate > 0.0f) {
                const float hzPerBin =
                    row.sampleRate / (2.0f * static_cast<float>(bins.size()));
                const int loBin = std::max(0,
                    static_cast<int>(loHz / hzPerBin));
                const int hiBin = std::min(static_cast<int>(bins.size()) - 1,
                    static_cast<int>(hiHz / hzPerBin));

                for (int col = 0; col < width; col++) {
                    const float frac0 = static_cast<float>(col)   / width;
                    const float frac1 = static_cast<float>(col+1) / width;
                    const int b0 = loBin + static_cast<int>(frac0 * (hiBin - loBin));
                    const int b1 = std::min(static_cast<int>(bins.size()),
                        loBin + static_cast<int>(frac1 * (hiBin - loBin)) + 1);
                    float peak = -120.0f;
                    for (int b = b0; b < b1; b++)
                        if (bins[b] > peak) peak = bins[b];
                    mvaddch(screenRow, col + 1, kSpecChars[dbToLevel(peak)]);
                }
            } else {
                mvhline(screenRow, 1, ' ', width);
            }
        } else {
            mvhline(screenRow, 1, ' ', width);
        }

        mvaddch(screenRow, width + 1, '|');
        attroff(COLOR_PAIR(PAIR_WATERFALL));
    }
}

void TuiWindow::drawColHeaders()
{
    const int bodyW = std::max(6, COLS - 49);

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvhline(kColHdrRow, 0, ' ', COLS);
    mvprintw(kColHdrRow, 0,
             "  %-7.7s %-13.13s %-13.13s %-*.*s %4s %5s",
             "TIME", "FROM", "TO",
             bodyW, bodyW, "BODY",
             "SNR", "+Hz");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
}

void TuiWindow::drawMessages()
{
    const int height = msgHeight();
    if (height <= 0) return;

    const int n = m_messages.size();
    const QString myCall = m_app->apiConfig().callsign.toUpper();
    const int bodyW = std::max(6, COLS - 49);

    // Clamp scroll so we can't scroll past the oldest message
    m_scroll = std::max(0, std::min(m_scroll, std::max(0, n - height)));

    for (int row = 0; row < height; row++) {
        const int screenRow = kMsgTop + row;
        if (screenRow >= msgBottom()) break;

        const int msgIdx = n - height - m_scroll + row;

        if (msgIdx < 0 || msgIdx >= n) {
            // Empty row — clear it
            attron(COLOR_PAIR(PAIR_DEFAULT));
            mvhline(screenRow, 0, ' ', COLS);
            attroff(COLOR_PAIR(PAIR_DEFAULT));
            continue;
        }

        const auto &msg  = m_messages[msgIdx];
        const QString ts = msg.utc.isValid()
                           ? msg.utc.toUTC().toString(QStringLiteral("HH:mmz"))
                           : QStringLiteral("------z");
        const bool toMe  = !myCall.isEmpty()
                           && msg.to.toUpper() == myCall;

        if (toMe) attron(COLOR_PAIR(PAIR_MSG_ME) | A_BOLD);
        else       attron(COLOR_PAIR(PAIR_MSG));

        mvprintw(screenRow, 0,
                 "  %-7.7s %-13.13s %-13.13s %-*.*s %+4d %5.0f",
                 ts.toLocal8Bit().constData(),
                 msg.from.left(13).toLocal8Bit().constData(),
                 msg.to.left(13).toLocal8Bit().constData(),
                 bodyW, bodyW,
                 msg.body.left(bodyW).toLocal8Bit().constData(),
                 msg.snrDb,
                 static_cast<double>(msg.freqHz));

        if (toMe) attroff(COLOR_PAIR(PAIR_MSG_ME) | A_BOLD);
        else       attroff(COLOR_PAIR(PAIR_MSG));
    }
}

void TuiWindow::drawInputLine()
{
    const int row = inputRow();
    if (row < 0 || row >= LINES) return;

    if (!m_statusMsg.isEmpty()) {
        // Temporary status message
        attron(COLOR_PAIR(PAIR_STATUS) | A_BOLD);
        mvhline(row, 0, ' ', COLS);
        mvprintw(row, 0, "  %.*s", COLS - 4,
                 m_statusMsg.toLocal8Bit().constData());
        attroff(COLOR_PAIR(PAIR_STATUS) | A_BOLD);
        curs_set(0);
    } else {
        // Input prompt
        const QString prompt = QStringLiteral("  > ") + m_input;
        attron(COLOR_PAIR(PAIR_INPUT) | A_BOLD);
        mvhline(row, 0, ' ', COLS);
        mvprintw(row, 0, "%.*s", COLS - 1,
                 prompt.toLocal8Bit().constData());
        attroff(COLOR_PAIR(PAIR_INPUT) | A_BOLD);
        // Position cursor after typed text
        const int curCol = std::min(COLS - 1, 4 + (int)m_input.length());
        move(row, curCol);
        curs_set(1);
    }
}

void TuiWindow::drawHints()
{
    const int row = hintsRow();
    if (row < 0 || row >= LINES) return;

    static const char kHints[] =
        "  Enter:send  /hb  /freq KHZ  /mode normal|fast|turbo|slow|ultra"
        "  /snr CALL  /info CALL  /clear  /halt  /q:quit  \u2191\u2193:scroll";

    attron(COLOR_PAIR(PAIR_DIM) | A_DIM);
    mvhline(row, 0, ' ', COLS);
    mvprintw(row, 0, "%.*s", COLS, kHints);
    attroff(COLOR_PAIR(PAIR_DIM) | A_DIM);
}

// ---------------------------------------------------------------------------
// Redraw
// ---------------------------------------------------------------------------

void TuiWindow::redraw()
{
    if (!m_dirty) return;
    m_dirty = false;

    curs_set(0);
    erase();

    drawTitleBar();
    drawSeparator(kSep1Row);
    drawWaterfall();
    drawSeparator(kSep2Row);
    drawColHeaders();
    drawMessages();
    drawSeparator(msgBottom());
    drawInputLine();
    drawHints();

    refresh();
}

// ---------------------------------------------------------------------------
// Keyboard input
// ---------------------------------------------------------------------------

void TuiWindow::onStdinReady()
{
    int key;
    while ((key = getch()) != ERR)
        handleKey(key);
}

void TuiWindow::handleKey(int key)
{
    switch (key) {
    case KEY_RESIZE:
        // Terminal was resized — let ncurses update LINES/COLS then redraw
        endwin();
        refresh();
        clear();
        m_dirty = true;
        break;

    case 3:   // Ctrl-C
        teardownNcurses();
        QCoreApplication::quit();
        break;

    case 27:  // ESC — clear input
        m_input.clear();
        m_dirty = true;
        break;

    case KEY_UP:
        ++m_scroll;
        m_dirty = true;
        break;

    case KEY_DOWN:
        m_scroll = std::max(0, m_scroll - 1);
        m_dirty = true;
        break;

    case KEY_PPAGE:  // Page Up
        m_scroll += std::max(1, msgHeight() - 1);
        m_dirty = true;
        break;

    case KEY_NPAGE:  // Page Down
        m_scroll = std::max(0, m_scroll - std::max(1, msgHeight() - 1));
        m_dirty = true;
        break;

    case KEY_HOME:
        // Scroll to oldest message
        m_scroll = std::max(0, (int)m_messages.size() - msgHeight());
        m_dirty  = true;
        break;

    case KEY_END:
        // Scroll to newest message
        m_scroll = 0;
        m_dirty  = true;
        break;

    case KEY_BACKSPACE:
    case 127:
    case 8:   // BS
        if (!m_input.isEmpty()) {
            m_input.chop(1);
            m_dirty = true;
        }
        break;

    case '\n':
    case '\r':
    case KEY_ENTER:
        if (!m_input.isEmpty())
            processInput();
        break;

    default:
        if (key >= 32 && key < 127) {
            m_input += QChar(static_cast<char>(key));
            m_dirty = true;
        }
        break;
    }
}

void TuiWindow::processInput()
{
    const QString text = m_input.trimmed();
    m_input.clear();
    m_dirty = true;

    if (text.isEmpty()) return;

    if (!text.startsWith('/')) {
        // Plain text — queue for TX as-is
        m_app->apiQueueTx(text);
        setStatus(QStringLiteral("Queued: ") + text);
        return;
    }

    // Command
    const QStringList parts = text.mid(1).split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;

    const QString cmd = parts[0].toLower();

    if (cmd == QLatin1String("q") || cmd == QLatin1String("quit")) {
        teardownNcurses();
        QCoreApplication::quit();
        return;
    }
    if (cmd == QLatin1String("hb")) {
        if (m_app->apiConfig().callsign.isEmpty()) {
            setStatus(QStringLiteral("Callsign not configured."));
            return;
        }
        m_app->apiQueueTx(m_app->apiConfig().callsign + QStringLiteral(" @HB"));
        setStatus(QStringLiteral("Heartbeat queued."));
        return;
    }
    if (cmd == QLatin1String("freq")) {
        if (parts.size() < 2) { setStatus(QStringLiteral("Usage: /freq KHZ")); return; }
        bool ok;
        const double khz = parts[1].toDouble(&ok);
        if (!ok || khz <= 0) {
            setStatus(QStringLiteral("Usage: /freq KHZ  (e.g. /freq 14078)"));
            return;
        }
        m_app->apiSetFrequency(khz);
        setStatus(QString("Frequency \u2192 %1 kHz").arg(khz, 0, 'f', 1));
        return;
    }
    if (cmd == QLatin1String("mode")) {
        if (parts.size() < 2) {
            setStatus(QStringLiteral("Usage: /mode normal|fast|turbo|slow|ultra"));
            return;
        }
        static const QStringList kModes =
            {"normal","fast","turbo","slow","ultra"};
        const int idx = kModes.indexOf(parts[1].toLower());
        if (idx < 0) {
            setStatus(QStringLiteral("Submodes: normal fast turbo slow ultra"));
            return;
        }
        m_app->apiSetSubmode(idx);
        setStatus(QString("Submode \u2192 %1").arg(kModes[idx]));
        return;
    }
    if (cmd == QLatin1String("snr")) {
        if (parts.size() < 2) { setStatus(QStringLiteral("Usage: /snr CALLSIGN")); return; }
        const QString dest = parts[1].toUpper();
        m_app->apiQueueTx(dest + QStringLiteral(" @SNR?"));
        setStatus(QString("Queued @SNR? \u2192 %1").arg(dest));
        return;
    }
    if (cmd == QLatin1String("info")) {
        if (parts.size() < 2) { setStatus(QStringLiteral("Usage: /info CALLSIGN")); return; }
        const QString dest = parts[1].toUpper();
        m_app->apiQueueTx(dest + QStringLiteral(" @INFO?"));
        setStatus(QString("Queued @INFO? \u2192 %1").arg(dest));
        return;
    }
    if (cmd == QLatin1String("clear")) {
        m_app->apiClearMessages();
        m_messages.clear();
        m_scroll = 0;
        setStatus(QStringLiteral("Messages cleared."));
        return;
    }
    if (cmd == QLatin1String("halt")) {
        m_app->apiClearTxQueue();
        setStatus(QStringLiteral("TX queue cleared."));
        return;
    }

    setStatus(QString("Unknown: /%1  (/hb /freq /mode /snr /info /clear /halt /q)")
              .arg(cmd));
}

// ── Stubs for non-ncurses builds ──────────────────────────────────────────────
#else  // !HAVE_NCURSES

int TuiWindow::inputRow()  const { return 0; }
int TuiWindow::hintsRow()  const { return 0; }
int TuiWindow::msgBottom() const { return 0; }
int TuiWindow::msgHeight() const { return 0; }

void TuiWindow::setupNcurses()         {}
void TuiWindow::teardownNcurses()      {}
void TuiWindow::drawTitleBar()         {}
void TuiWindow::drawSeparator(int)     {}
void TuiWindow::drawWaterfall()        {}
void TuiWindow::drawColHeaders()       {}
void TuiWindow::drawMessages()         {}
void TuiWindow::drawInputLine()        {}
void TuiWindow::drawHints()            {}
void TuiWindow::redraw()               {}
void TuiWindow::onStdinReady()         {}
void TuiWindow::handleKey(int)         {}
void TuiWindow::processInput()         {}

#endif  // HAVE_NCURSES
