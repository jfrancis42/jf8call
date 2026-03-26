# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working Style and Collaboration

~/please use the working style and collaboration as detailed in ~/CLAUDE.md

---

## Dependencies

Whenever a new Linux system package is required:
1. **Install it on this development machine** immediately
2. Update `CMakeLists.txt` with the new find_package / pkg_check_modules call
3. Update `CLAUDE.md` (this file) under the relevant dependency section

### Build dependencies (Ubuntu/Debian x86_64 host)

Native Linux build:
```
qt6-base-dev libqt6websockets6-dev libqt6multimedia6-dev libhamlib-dev cmake ninja-build libportaudio-dev libkissfft-dev libkissfft-float131
```

Windows cross-compile:
```
mingw-w64 cmake ninja-build python3-pip
pip install aqtinstall
# Qt6 Windows: ~/Qt-cross/windows/6.9.2/mingw_64/
# Hamlib Windows: ~/Qt-cross/hamlib-windows/
# PortAudio Windows: prebuilt MinGW DLL
```

Runtime:
```
libqt6widgets6 libhamlib4 libportaudio2 libkissfft-float131
```

---

## System Overview

Company: Ordo Artificum LLC
Product Name: JF8Call

Goal: JF8Call is a Qt6/C++ JS8Call-compatible application for Linux and Windows.
It uses ~/gfsk8-modem-clean/ as the core modem library and implements:
- Real-time JS8 receive with waterfall display
- JS8 transmit with period-synchronized transmission
- All five submodes (Normal/Fast/Turbo/Slow/Ultra)
- Hamlib radio control (frequency, PTT)
- @ message protocol (@HB heartbeat, @SNR?, @INFO?, directed messages)
- WebSocket API on ws://localhost:2102 (see websocket-api.md)
- Basic store-and-forward message design (infrastructure only; relay not yet built)
- TUI design (not yet built)

## MANDATORY: GUI / WebSocket API Parity

**Every capability exposed in the GUI MUST also be accessible via the WebSocket API,
and vice versa. This is a hard requirement, not a guideline.**

When adding a new feature:
1. Implement it in the GUI (MainWindow).
2. Expose it via a `MainWindow::apiXxx()` public method.
3. Wire a WS command handler in WsServer that calls the `apiXxx()` method.
4. Emit the appropriate WS push event so clients receive unsolicited notification.
5. Document the new command and event in `websocket-api.md`.

No exceptions. A feature that exists only in the GUI or only in the API is a bug.

License: GPL-3.0 (required by gfsk8-modem-clean dependency)

---

## Architecture

- Qt6 / C++20
- **GUI**: QMainWindow + QSplitter (OTA visual style — dark theme, amber accents)
- **WebSocket API**: QWebSocketServer on localhost:2102 (WsServer class) — 100% feature parity with GUI
- **TUI**: designed but not yet built (will use --text flag like OTA)
- Hamlib for radio control (optional — guarded with `#ifdef HAVE_HAMLIB`)
- PortAudio for cross-platform audio I/O (required)
- KissFFT for waterfall FFT (via gfsk8-modem-clean vendored library)
- ~/gfsk8-modem-clean/ as the core modem (static library, GPL-3.0)

---

## Source Files

```
src/main.cpp              — Entry point; --text → TuiWindow (NYI); otherwise → MainWindow
src/mainwindow.h/.cpp     — QMainWindow: waterfall, message panels, toolbar, status bar
                            Public apiXxx() methods are the WS API surface; ALL GUI actions
                            must have a corresponding apiXxx() method.
src/wsserver.h/.cpp       — QWebSocketServer: WebSocket API (see websocket-api.md)
                            All WS commands call MainWindow::apiXxx() methods.
                            All state changes push events to connected clients.
src/config.h/.cpp         — Config struct + JSON persistence (~/.jf8call/settings.json)
src/hamlibcontroller.h/.cpp — Hamlib: connect/disconnect, setFrequency, setPtt, poll
src/audioinput.h/.cpp     — PortAudio input thread → FIR decimate → decoder feed
src/audiooutput.h/.cpp    — PortAudio output thread → plays modulated audio
src/waterfallwidget.h/.cpp — QWidget custom painting: scrolling spectrogram (2048-pt FFT)
src/messagemodel.h/.cpp   — QAbstractTableModel for received messages
src/js8message.h/.cpp     — JS8 message parsing, @ command handling, DecodedText wrapper
src/periodclock.h/.cpp    — JS8 period boundary timer (UTC-aligned QTimer)
```

---

## Visual Style

Same as OTA. Dark theme throughout. Key colors:
- Background: `#1a1a2e` (main), `#16213e` (panels/toolbar/statusbar)
- Default text: `#e8e0d0`
- Amber accent: `#c9a84c` (headers, toolbar labels, active freq)
- Green: `#7fbf7f` (received messages)
- Blue highlight: selected row `#8b6914`
- Waterfall: black background, color map black→navy→cyan→yellow→red (power)
- TX indicator: red `#cc2222` when transmitting

---

## Audio Chain

RX:
```
Hardware (e.g. IC-7300 USB audio) → PortAudio 48 kHz stereo
  → left channel only
  → 4:1 FIR decimation (49-tap Kaiser-windowed lowpass, fc=5400 Hz, Kaiser β=6)
  → 12 kHz float32 ring buffer
  → [waterfall] 2048-pt FFT at ~10 Hz update rate
  → [decoder] accumulate full period, convert to int16, call gfsk8::Decoder::decode()
```

TX:
```
User message → gfsk8::pack() → vector<TxFrame>
  → gfsk8::modulate() per frame → 12 kHz float32 PCM
  → 4:1 linear interpolation upsample → 48 kHz int16
  → PortAudio output
  → Hamlib setPtt(true) before first sample / setPtt(false) after last
```

The FIR coefficients (49 taps) are the same as in ~/gfsk8-modem-clean/apps/js8rx/main.cpp.

---

## JS8 Period Clock

JS8 periods are UTC-aligned:
- Normal (15s): :00, :15, :30, :45
- Fast (10s):   :00, :10, :20, :30, :40, :50
- Turbo (6s):   :00, :06, :12, ...
- Slow (30s):   :00, :30
- Ultra (4s):   :00, :04, :08, ...

`PeriodClock` fires a signal at each period boundary for the active submode.
The decoder is triggered at the period boundary with the just-completed audio buffer.
TX is started at the period boundary + start_delay_ms for the active submode.

---

## @ Message Protocol

Implemented in js8message.cpp:

| @ Command     | Description                             | Auto-reply       |
|---------------|-----------------------------------------|------------------|
| @HB           | Heartbeat (broadcast every N periods)   | None             |
| CALL @HB      | Directed heartbeat                      | None             |
| CALL @SNR?    | Request SNR report                      | CALL SNR +xx dB  |
| CALL @INFO?   | Request station info                    | CALL INFO ...    |
| CALL @?       | General directed message request        | n/a              |
| CALL MSG      | Directed message (any text after CALL)  | n/a              |

Auto-reply behavior:
- @SNR?: reply with measured SNR of the sender's signal
- @INFO?: reply with callsign, grid, submode, software version

---

## ACK Protocol

Auto-ACK rules (implemented in `mainwindow.cpp::onDecodeFinished`):

**Only ACK `MSG` (store-and-forward) commands. Never ACK generic directed freetext.**

The JS8 frame type bits (`Varicode::TransmissionType` in `gfsk8-modem-clean/src/Varicode.h`):
- `JS8Call = 0` — middle frame of a multi-frame message
- `JS8CallFirst = 1` — first frame
- `JS8CallLast = 2` — last frame of a multi-frame message
- `FrameDirected = 3` (= JS8CallFirst | JS8CallLast) — single-frame directed message

Rules:
1. **Wait for the last frame** before sending ACK: check `(d.frameType & Varicode::JS8CallLast) != 0`.
   `FrameDirected` (single-frame) has this bit set, so it is handled correctly too.
2. **Only ACK `MsgCommand` type** (`msg.type == JS8Message::Type::MsgCommand`).
   `DirectedMessage` (generic freetext directed to you) does NOT get an auto-ACK.

What does NOT trigger an ACK:
- First or middle frames of any multi-frame message
- Generic directed freetext (`JS8Message::Type::DirectedMessage`)
- Heartbeats, query commands, query responses

---

## GFSK8 Multi-Frame Assembly

JS8 messages can span multiple 15-second (or shorter) periods. The application must buffer frames and wait for the last frame before processing.

**Frame type bits** (same as ACK Protocol section above):
- `FrameDirected = 3` — single-frame complete message; process immediately
- `JS8CallFirst = 1` (only) — first of a multi-frame sequence; buffer it
- `JS8Call = 0` (no bits) — middle continuation frame; append to buffer
- `JS8CallLast = 2` (only) — final frame; assemble complete text, then process once

**Assembly logic** (`mainwindow.cpp::onDecodeFinished`, `m_gfsk8FrameBuffers`):
- Buffer keyed by `round(freqHz / 10)` (same key as heard pane grouping).
- Heard pane updates in real time for every frame (partial display is fine).
- Model row, WS push, auto-reply, and auto-ACK are deferred until the last frame.
- On last frame: prepend all buffered `rawText`, call `parseDecoded` once on the assembled text.

**Timeout cleanup** (`onFrameCleanupTimer`, fires every 15 s):
- Buffer age > 60 s: force-process with ` ♦?` marker (incomplete message)
- Buffer age > 90 s: discard silently

**EOM display**: After completing a GFSK8 message (last or single frame), ` ♦` is appended to the heard pane line for that frequency.

---

## End-of-Message and Sync Markers (Streaming Modems)

For PSK, Olivia, and Codec2 — which have no inherent frame boundaries — JF8Call embeds invisible control characters in the transmitted payload. These are detected and stripped on receive.

### Constants (defined in `src/imodem.h`)

```cpp
kStreamSyncChar     = '\x05'  // ENQ — periodic sync heartbeat
kStreamEomChar      = '\x04'  // EOT — end of message
kStreamSyncInterval = 64      // inject sync every 64 chars (PSK, Olivia)
kCodec2SyncInterval = 4       // inject sync every 4 data frames (Codec2)
```

### PSK and Olivia

**TX (`pack()`)**: Injects `\x05` every 64 characters in the payload; appends `\x04` as the final character.

**RX (`charCb()` / feedAudio lambda)**:
- `\x05` received → emit `ModemDecoded{isSyncMark=true}` (no display text); do NOT add to accumulation buffer
- `\x04` received → flush current accumulation buffer as a `ModemDecoded{isEom=true}`

### Codec2 DATAC

**TX (`pack()`)**: Inserts a sync frame (`kFlagSync = 0x02`, CRC-protected, empty payload) every 4 data frames; appends an EOM frame (`kFlagEom = 0x04`) after the last data frame.

**RX (`feedAudio()`)**: Frame flag byte dispatch:
- `0x01` (`kFlagText`) → normal text frame, existing behavior
- `0x02` (`kFlagSync`) → emit `ModemDecoded{isSyncMark=true}`
- `0x04` (`kFlagEom`) → emit `ModemDecoded{isEom=true}`
- Any other flag → skip

### MainWindow handling

- `isSyncMark=true`: consumed silently (no display, no model update)
- `isEom=true`: processed as a normal message; after the heard pane update, ` ♦` is appended to the line for that frequency
- `isEom=false` (normal streaming message): no EOM marker shown

### Interoperability note

Non-JF8Call PSK software will transmit without these markers (no sync events, no EOM display — graceful degradation). On receive from non-JF8Call software, `\x04` at the end of a received PSK message will appear as a trailing garbage character. This is a known trade-off.

---

## Store-and-Forward Design (not yet implemented)

Architecture planned:
- `MessageStore`: SQLite or JSON file of pending relay messages
- Each message: {from, to, via_relay, body, received_utc, ttl_periods}
- Relay: if a directed message is heard for a station that has "relay requested",
  re-send it if the destination has not been heard recently
- UI: separate "Inbox" tab showing messages addressed to mycall

---

## WebSocket API

See `websocket-api.md` for the full API reference.

Key design points:
- All messages are UTF-8 JSON text frames.
- Client sends commands (`{"type":"cmd","cmd":"...","data":{...}}`).
- Server replies with `{"type":"reply","ok":true,"data":{...}}`.
- Server pushes events unsolicited to all clients on any state change.
- Spectrum throttled to ~5 Hz via internal timer even though audio FFT runs at ~10 Hz.
- Status event pushed at 1 Hz for heartbeat / polling clients.

---

## Hamlib Notes

Same pattern as ~/ota/src/hamlibcontroller.h:
- `rig_load_all_backends()` before `rig_list_foreach()`
- `setPtt(bool)` for CAT PTT control
- `setFrequency(khz)` and `getFrequency()` for frequency tracking
- Polling at 2 Hz while connected to track VFO changes
- Guarded with `#ifdef HAVE_HAMLIB`
- Rig model, port, baud rate persisted in settings.json

---

## Config Persistence

Location: `~/.jf8call/settings.json`

Key fields:
```json
{
  "callsign":     "W5XYZ",
  "grid":         "DM79AA",
  "audioInput":   "",
  "audioOutput":  "",
  "submode":      0,
  "frequency_khz": 14078.0,
  "txFreqHz":     1500.0,
  "txPowerPct":   50,
  "heartbeatEnabled": true,
  "heartbeatIntervalPeriods": 4,
  "autoReply":    true,
  "rigModel":     1,
  "rigPort":      "",
  "rigBaud":      9600,
  "windowGeometry": "...",
  "windowState":    "..."
}
```

---

## Build System

### Native Linux (development)
```bash
sh ./autogen.sh          # check dependencies
./configure              # configure (wraps cmake)
make                     # build
# Binary: build/jf8call
```

Or directly with cmake:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGFSK8MODEM_DIR=~/gfsk8-modem-clean
cmake --build build --parallel
```

### Linux AppImage
```bash
./package-appimage.sh
# Output: JF8Call-0.1.0-ALPHA-x86_64.AppImage
```

### Windows (cross-compile from Linux)
```bash
./build-windows-cross.sh --setup   # one-time: downloads Qt6 + Hamlib to ~/Qt-cross/
./build-windows-cross.sh           # builds jf8call.exe + DLLs in build-windows/deploy/
```
Note: PortAudio Windows DLL must be obtained separately and placed in
`~/Qt-cross/portaudio-windows/` (see script for details).

### Raspberry Pi (aarch64 cross-compile)
```bash
./build-raspberrypi.sh --setup   # one-time: installs arm64 packages
./build-raspberrypi.sh           # builds build-rpi/jf8call (ELF aarch64)
```

---

## Common Pitfalls

- **PortAudio device index**: changes between sessions. Persist device *name* not index.
  On startup, scan for a device matching the saved name; fall back to default.
- **Period boundary jitter**: `PeriodClock` uses `QTimer::singleShot` recalculated each
  period from `QDateTime::currentDateTimeUtc()` to prevent drift accumulation.
- **TX/RX half-duplex**: most HF radios can't receive while transmitting. When TX is
  active, stop feeding audio to the decoder; resume after PTT drops.
- **int16 overflow on modulate upsample**: clamp interpolated samples to [-32767, 32767].
- **gfsk8::Decoder is heavy**: create once at startup, reuse every period.
- **MOC vtable errors**: clean build directory after adding new Q_OBJECT classes.
