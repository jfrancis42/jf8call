# JF8Call TODO — Missing Features from JS8Call-improved

Features present in JS8Call-improved (W6BAZ fork) that are absent or incomplete in JF8Call,
ranked by implementation priority for JF8Call's headless/API-first use case.

---

## Priority 1 — Core JS8 Protocol Compliance

### 1. Persistent Inbox & Store-and-Forward
**Why first:** Store-and-forward is _the_ defining feature of JS8 that makes it viable for
emergency communications and unattended network nodes. JF8Call currently has only a skeleton
`messageinbox.h/cpp` with no persistence. Without this, JF8Call cannot meaningfully participate
in a JS8 network.

- Persistent inbox backed by SQLite (survives restarts)
- Store-and-forward relay: accept, hold, and re-transmit messages destined for other stations
- Inbox query/delivery via @MSG commands
- Expose inbox contents via WebSocket API (already partially designed)

**Reference:** `JS8_Main/Inbox.cpp`, `JS8_Main/Message.cpp`

---

### 2. Message Relay Server & Client
**Why second:** Once inbox is persistent, a relay server allows JF8Call to act as a mesh node —
forwarding messages between stations that cannot hear each other directly. This is the JS8
network's backbone feature and aligns directly with JF8Call's "integration layer" design goal.

- TCP relay server (JS8Call uses port 2442)
- Client connection management with reconnect logic
- Relay routing: accept inbound relayed messages and store in local inbox
- Expose relay status and relayed message events via WebSocket API

**Reference:** `JS8_Main/MessageServer.cpp`, `JS8_Main/MessageClient.cpp`

---

## Priority 2 — Operator Essentials

### 3. QSO Logging & ADIF Support
**Why third:** Every licensed amateur expects logging. Without it, JF8Call is not a complete
station application, and operators must maintain a separate log manually. ADIF is the universal
exchange format for log data.

- SQLite-backed QSO log (callsign, freq, mode, RST, date/time, grid, name, comment)
- ADIF export (minimum: ADIF 3 format)
- ADIF import for pre-existing logs
- Auto-log on confirmed QSO (configurable threshold)
- Expose log query/append/export via WebSocket API
- Country/entity lookup (cty.dat integration optional but useful)

**Reference:** `JS8_Logbook/LogBook.cpp`, `JS8_Logbook/ADIF.cpp`, `JS8_Logbook/CountriesWorked.cpp`

---

## Priority 3 — Operational Utility

### 5. Band & Frequency Management
**Why fifth:** Operators work multiple bands. Currently JF8Call stores a single frequency in
config. A proper frequency/band model enables per-band defaults, dial/offset separation,
and future multi-VFO support.

- Band definitions with frequency ranges (160m–70cm minimum)
- Frequency memory/presets (save/recall named frequencies)
- IARU region awareness (region-specific band plans)
- Expose band list and presets via WebSocket API

**Reference:** `JS8_Main/Bands.cpp`, `JS8_Main/FrequencyList.cpp`, `JS8_Main/IARURegions.cpp`

---

## Priority 4 — Network Integration

### 7. APRS-IS Integration
**Why seventh:** JS8Call's APRS gateway is used in emergency networks to relay position reports
from radio-only stations onto the APRS-IS internet backbone, and vice versa. Aligns with
JF8Call's integration/automation use case.

- APRS-IS client (TCP connection to rotate.aprs.net or configurable server)
- Inbound relay: decode JS8 APRS-format messages and forward to APRS-IS
- Outbound: optionally beacon own position to APRS-IS
- Expose APRS relay status and events via WebSocket API

**Reference:** `JS8_Main/APRSISClient.cpp`, `JS8_Main/AprsInboundRelay.cpp`

---

### 8. DX Spot Client
**Why eighth:** Spot reporting allows other operators to find active JS8 stations. Lower priority
than APRS because PSKReporter (already implemented) covers the primary discovery use case.

- DX Cluster TCP client
- Parse and emit spot events via WebSocket API
- Submit received JS8 spots to cluster (configurable)

**Reference:** `JS8_Network/SpotClient.cpp`

---

## Priority 5 — Rig Control Expansion

### 9. Additional Radio Control Backends
**Why ninth:** Hamlib covers the vast majority of radios. Additional backends only matter for
operators locked into DX Lab Suite or Ham Radio Deluxe.

- DX Lab Suite Commander (TCP)
- Ham Radio Deluxe (TCP)
- Polling/emulated split support for radios that need it

**Reference:** `JS8_Transceiver/DXLabSuiteCommanderTransceiver.cpp`,
`JS8_Transceiver/HRDTransceiver.cpp`, `JS8_Transceiver/PollingTransceiver.cpp`

---

## Priority 6 — UI Enhancements

### 10. Advanced Waterfall / Spectrum Display
**Why last:** JF8Call's headless-first design makes UI polish the lowest operational priority.
The waterfall works; these are refinements.

- Multiple spectrum averaging modes (Cumulative, Linear Average, Current) matching WideGraph
- Frequency pill rendering for active signals
- Directed message syntax highlighting in activity log
- Custom signal strength meter widget

**Reference:** `JS8_Main/CPlotter.cpp`, `JS8_Main/PillRenderer.cpp`,
`JS8_Main/DirectedMessageHighlighter.cpp`

---

---

## Implementation Results (2026-03-26)

All items listed above (except #8 DX Spot Client) were implemented in this session.
Build artifacts produced and verified:

| Artifact | Path | Status |
|---|---|---|
| Linux x86_64 AppImage | `JF8Call-0.5.2-ALPHA-x86_64.AppImage` | ✓ built & copied to greybox |
| Windows x86_64 | `build-windows/deploy/jf8call.exe` | ✓ built |
| Raspberry Pi aarch64 | `build-rpi/jf8call` | ✓ built |

### Item 1 — Persistent Inbox & Store-and-Forward
`MessageInbox` was already JSON-backed (survives restarts). The new work:
- **Relay server** (`src/relayserver.h/cpp`): `QTcpServer` on port 2442. Accepts newline-delimited JSON commands: `store`, `list`, `delivered`, `ping`. Deposited messages go directly into `MessageInbox`.
- WebSocket `inbox.get` command exposes inbox contents.

### Item 2 — Message Relay Server & Client
Implemented as `RelayServer` (see above). The TCP relay server is started on application launch
if `config.relayServerEnabled` is true (default: false). Port and localhost-only flag are
configurable in settings. The server emits `messageDeposited(InboxMessage)` which MainWindow
can use to trigger WS push events.

### Item 3 — QSO Logging & ADIF Support
- `src/qsolog.h/cpp`: `QsoLog` singleton, SQLite-backed via `Qt6::Sql`. Table: `id`, `utc`,
  `callsign`, `grid`, `band`, `mode`, `freq_khz`, `tx_freq`, `snr`, `sent_rst`, `rcvd_rst`, `notes`.
- ADIF 3.1.0 export (`exportAdif()`) and import (`importAdif()`).
- GUI: File → QSO Log… (table dialog with log/delete/export/import ADIF); File → Export ADIF…
- WebSocket: `qso.log.get`, `qso.log.adif`
- `MainWindow::apiLogQso()` called automatically from `onDecodeFinished` for confirmed QSOs
  (`JF8Message::Type::MsgCommand` with last frame received).

### Item 5 — Band & Frequency Management
- `src/freqschedule.h`: `BandPreset` struct + `standardBandPresets()` list of 12 bands
  (160m–2m), `bandForFreqKhz()` helper.
- Band preset combo box in toolbar; selecting a band sets both dial frequency and TX offset.
- Automatic frequency schedule: entries with UTC HHMM, day-of-week bitmask, target
  frequency/offset. Checked every minute via `QTimer`. GUI: File → Freq Schedule…
- WebSocket: `freq.schedule.get`, `freq.schedule.set`

### Item 7 — APRS-IS Integration
- `src/aprsclient.h/cpp`: TCP client to configurable APRS-IS server (default: `rotate.aprs.net:14580`).
- Static `computePasscode(callsign)` implements the XOR-based APRS-IS passcode algorithm.
- Login line sent on connect; auto-reconnects every 60 s on disconnect.
- Emits `packetReceived(QString)` for inbound APRS packets (non-comment lines).
- Enabled when `config.aprsEnabled = true`.

### Item 9 (partial) — Emulated Split Radio Control
- `RigConfig` gains `emulatedSplit` and `txFreqKhz` fields.
- `HamlibController::setPtt()`: if emulated split is active and `txFreqKhz > 0`,
  captures RX frequency before PTT-up, switches to TX frequency, then returns to RX
  frequency on PTT-down — transparent to the rest of the application.

### Item 10 — Advanced Waterfall Display
- `WaterfallWidget` gains three display modes:
  - **Current** (0): raw FFT line, same as before
  - **Linear Average** (1): exponential-decay running average (τ ≈ 4 lines)
  - **Cumulative** (2): per-bin peak hold
- Gain slider (–30 dB to +30 dB) in waterfall control bar above waterfall.
- Mode and gain overlaid as text on the waterfall when non-default.
- Config persists `waterfallGain` and `waterfallMode`.

### Extra — Base Frequency in WebSocket Push Data
- `pushMessageDecoded` and `pushMessageFrame` now include:
  - `dial_freq_khz`: radio dial frequency (from config)
  - `spot_freq_khz`: dial + audio offset / 1000 (absolute spot frequency in kHz)

### Extra — Scheduled Frequency Changes GUI
- File → Freq Schedule… opens a dialog listing all schedule entries with checkboxes.
- Add/Remove buttons; each entry configures UTC time, day-of-week bitmask, target
  dial frequency (kHz), TX offset (Hz), and optional label.
- Checked every minute; fires at the first matching minute boundary.

### Extra — Solar Data Panel
- `src/solardata.h/cpp`: `SolarDataFetcher` fetches from two NOAA SWPC JSON endpoints
  every 15 minutes: `noaa-scales.json` (G/S/R geomagnetic scales, K-index) and
  `solar-geophysical-activity.json` (SFI, SSN, X-ray class).
- Displayed in a label in the status bar: e.g. `SFI:142  SSN:89  A:12  K:3  X:C1.4  G:1 S:0 R:0`
- K-index colourises label: green (0–2), yellow (3–4), orange (5), red (6+).
- WebSocket: `solar.get`

### Extra — Waterfall Intensity Slider
See Item 10 above (gain slider in waterfall control bar).

---

## Out of Scope

The following JS8Call-improved features are **intentionally not planned** for JF8Call, as they
conflict with its design goals or are already superseded by JF8Call's architecture:

- Qt `.ui` file-based dialog system (JF8Call uses programmatic Qt)
- Monolithic built-in modem (JF8Call uses pluggable external modem libraries)
- WSJT-X UDP protocol compatibility (JF8Call uses WebSocket API instead)
- MetaDataRegistry / MultiSettings system (JF8Call uses simple JSON config)
- TraceFile debug system (use standard logging instead)
