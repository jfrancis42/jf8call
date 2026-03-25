#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// JF8Call — JS8Call-compatible application
// Copyright (C) 2026 Ordo Artificum LLC

#include <QString>

// All persistent configuration for JF8Call.
// Loaded from / saved to ~/.jf8call/settings.json.
struct Config {
    // Station identity
    QString callsign;
    QString grid;

    // Audio
    QString audioInputName;    // PortAudio device name; empty = default
    QString audioOutputName;   // PortAudio device name; empty = default

    // Modem selection
    int     modemType    = 0;          // 0=Gfsk8 (JS8-compatible), 1=Codec2 DATAC

    // Operating parameters (submode index is per-modem)
    int     submode      = 0;          // Gfsk8: 0=Normal…4=Ultra; Codec2: 0=DATAC0…2=DATAC3
    double  frequencyKhz = 14078.0;   // dial frequency (kHz)
    double  txFreqHz     = 1500.0;    // audio TX offset (Hz within passband)
    int     txPowerPct   = 50;        // 0-100

    // Station identity extras (shown in auto-replies)
    QString stationInfo;    // custom info text for @INFO? replies (empty = default)
    QString stationStatus;  // custom status text for @STATUS?/@? replies (empty = "HEARD")
    QString cqMessage;      // CQ message sent by the CQ button

    // Auto-behaviour
    bool    heartbeatEnabled          = true;
    int     heartbeatIntervalPeriods  = 4;   // transmit @HB every N periods
    bool    autoReply                 = true; // auto-reply to @SNR? and @INFO?
    bool    distMiles                 = true; // true=miles false=km
    bool    autoAtu                   = false;// trigger ATU on band change

    // Hamlib / radio
    int     rigModel    = 1;           // 1 = Dummy (safe default)
    QString rigPort;                   // e.g. "/dev/ttyUSB0" or "localhost:4532"
    int     rigBaud     = 9600;
    int     rigDataBits = 8;
    int     rigStopBits = 1;           // 1 or 2
    int     rigParity   = 0;           // 0=None 1=Odd 2=Even
    int     rigHandshake= 0;           // 0=None 1=XON/XOFF 2=Hardware
    int     rigDtrState = 0;           // 0=unset 1=on 2=off
    int     rigRtsState = 0;           // 0=unset 1=on 2=off
    int     pttType     = 0;           // 0=VOX/none 1=CAT 2=DTR 3=RTS

    // WebSocket API
    bool    wsEnabled  = true;
    int     wsPort     = 2102;
    QString wsHost     = QStringLiteral("127.0.0.1");  // "127.0.0.1" or "0.0.0.0"

    // Window state
    QByteArray windowGeometry;
    QByteArray windowState;
    QByteArray vSplitterState;   // outer vertical splitter
    QByteArray hSplitterState;   // inner horizontal splitter

    // Persistence
    static Config load();
    void save() const;

private:
    static QString filePath();
    static void ensureDir();
};
