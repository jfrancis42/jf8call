#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call — JS8Call-compatible application
// Copyright (C) 2026 Ordo Artificum LLC
//
// TuiWindow — ncurses-based terminal user interface.
// Driven by MainWindow signals; uses apiXxx() methods for all actions.
// Launched when jf8call is started with --text.

#include <QObject>
#include <QDateTime>
#include <QList>
#include <QVector>
#include <QTimer>
#include <QSocketNotifier>
#include <vector>
#include "js8message.h"

class MainWindow;

class TuiWindow : public QObject {
    Q_OBJECT
public:
    explicit TuiWindow(MainWindow *app, QObject *parent = nullptr);
    ~TuiWindow();

    void start();

private slots:
    void onMessageDecoded(const JS8Message &msg);
    void onSpectrumReady(std::vector<float> bins, float sampleRate);
    void onTxStarted();
    void onTxFinished();
    void onRadioStatusChanged();
    void onStdinReady();
    void redraw();

private:
    // ── Fixed layout rows (counted from the top) ──────────────────────────
    static constexpr int kTitleRow  = 0;
    static constexpr int kSep1Row   = 1;
    static constexpr int kWfTop     = 2;
    static constexpr int kWfRows    = 5;
    static constexpr int kSep2Row   = kWfTop  + kWfRows;   // 7
    static constexpr int kColHdrRow = kSep2Row + 1;         // 8
    static constexpr int kMsgTop    = kColHdrRow + 1;       // 9

    static constexpr int kMaxMessages  = 500;

    // ── Dynamic layout (bottom of terminal depends on LINES) ───────────────
    int inputRow()  const;   // LINES - 2
    int hintsRow()  const;   // LINES - 1
    int msgBottom() const;   // LINES - 3  (separator row before input)
    int msgHeight() const;   // msgBottom() - kMsgTop

    // ── ncurses lifecycle ──────────────────────────────────────────────────
    void setupNcurses();
    void teardownNcurses();

    // ── Drawing ────────────────────────────────────────────────────────────
    void drawTitleBar();
    void drawSeparator(int row);
    void drawWaterfall();
    void drawColHeaders();
    void drawMessages();
    void drawInputLine();
    void drawHints();

    // ── Input ──────────────────────────────────────────────────────────────
    void handleKey(int key);
    void processInput();
    void setStatus(const QString &msg, int durationMs = 3000);

    // ── Members ────────────────────────────────────────────────────────────
    MainWindow       *m_app;
    QSocketNotifier  *m_stdinNotifier = nullptr;
    QTimer           *m_redrawTimer   = nullptr;
    QTimer           *m_statusTimer   = nullptr;
    bool              m_ncursesActive = false;

    // Decoded message ring buffer
    struct MsgEntry {
        QDateTime utc;
        float     freqHz = 0.0f;
        int       snrDb  = 0;
        QString   from, to, body;
    };
    QList<MsgEntry>  m_messages;
    int              m_scroll = 0;   // rows scrolled up from the bottom

    // Spectrum waterfall ring buffer (newest at back)
    struct SpecRow { std::vector<float> bins; float sampleRate = 12000.0f; };
    QVector<SpecRow>  m_waterfall;

    // Input / state
    QString  m_input;
    bool     m_txActive = false;
    bool     m_dirty    = true;
    QString  m_statusMsg;
};
