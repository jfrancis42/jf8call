# JF8Call API Programmer's Guide

This guide covers everything you need to integrate with JF8Call programmatically:
the WebSocket API protocol, all commands and events, the `jf8-tool.py` command-line
tool, and worked examples in Python.

---

## Table of Contents

1. [Overview](#overview)
2. [Connecting](#connecting)
3. [Message Protocol](#message-protocol)
4. [jf8-tool Reference](#jf8-tool-reference)
5. [Commands Reference](#commands-reference)
   - [status.get](#statusget)
   - [config.get / config.set](#configget--configset)
   - [audio.devices / audio.restart](#audiodevices--audiorestart)
   - [radio.get / radio.connect / radio.disconnect](#radio-commands)
   - [radio.frequency.set / radio.ptt.set](#radiofrequencyset--radiopttset)
   - [messages.get / messages.clear](#messagesget--messagesclear)
   - [spectrum.get](#spectrumget)
   - [tx.send](#txsend)
   - [tx.queue.get / tx.queue.clear](#txqueueget--txqueueclear)
   - [tx.hb / tx.snr / tx.info / tx.status](#protocol-tx-commands)
6. [Events Reference](#events-reference)
7. [Python Integration Examples](#python-integration-examples)
8. [Design Notes](#design-notes)

---

## Overview

JF8Call exposes a **WebSocket API** on `ws://localhost:2102` (port configurable in
Preferences → WebSocket API). The API provides 100% programmatic access to every
capability available in the GUI — there are no GUI-only features.

**Key characteristics:**

- All messages are UTF-8 JSON text frames (no binary frames).
- Clients send *commands*; the server sends *replies* and *events*.
- Events are broadcast to **all** connected clients whenever application state changes
  — no subscription is needed.
- The server pushes a `status` event at 1 Hz so polling clients can stay in sync.
- Multiple clients can connect simultaneously.
- The server listens on `127.0.0.1` by default; set host to `0.0.0.0` in Preferences
  to allow LAN access.

**Dependencies for `jf8-tool.py` and the Python examples below:**

```bash
pip install websockets
```

---

## Connecting

On connection the server immediately sends two unsolicited messages:

```json
{"type":"hello","data":{"message":"JF8Call WebSocket API","version":"1.0"}}
{"type":"event","event":"status","data":{ ... full status object ... }}
```

Your client should drain these before sending its first command, or it must be
prepared to receive events interleaved with replies at any time.

---

## Message Protocol

### Commands (client → server)

```json
{
  "type": "cmd",
  "id": "abc123",
  "cmd": "command.name",
  "data": { "key": "value" }
}
```

- `id` — optional opaque string; echoed back in the reply for correlation.
  Use a UUID or sequential counter to match replies to requests.
- `data` — optional; not all commands require it.

### Replies (server → client)

```json
{ "type": "reply", "id": "abc123", "ok": true,  "data": { ... } }
{ "type": "reply", "id": "abc123", "ok": false, "error": "description" }
```

Replies are sent only to the client that issued the command.

### Events (server → all clients)

```json
{ "type": "event", "event": "event.name", "data": { ... } }
```

Events arrive asynchronously. A robust client reads all incoming messages in a loop
and routes them by `type`:

```python
async def read_loop(ws):
    async for raw in ws:
        msg = json.loads(raw)
        if msg["type"] == "reply":
            handle_reply(msg)
        elif msg["type"] == "event":
            handle_event(msg["event"], msg.get("data", {}))
```

---

## jf8-tool Reference

`jf8-tool.py` is a ready-to-use command-line interface for the JF8Call WebSocket API.
It connects, runs a single command, prints the result, and exits — except for
`monitor` mode, which streams events until interrupted.

### Installation / setup

```bash
pip install websockets
chmod +x jf8-tool.py
```

### Global options

```
--host HOST    JF8Call host (default: localhost)
--port PORT    WebSocket port (default: 2102)
```

These go before the command name:

```bash
./jf8-tool.py --host 192.168.1.10 status
./jf8-tool.py --port 2103 config get
```

### Command summary

| Command | Description |
|---------|-------------|
| `status` | Show current application state |
| `config get` | Print full configuration as JSON |
| `config set key=value ...` | Update one or more config fields |
| `audio devices` | List available PortAudio devices |
| `audio restart` | Restart audio I/O |
| `radio get` | Show rig connection status |
| `radio connect [options]` | Connect to rig via Hamlib |
| `radio disconnect` | Disconnect from rig |
| `radio freq KHZ` | Set VFO frequency in kHz |
| `radio tune` | Trigger ATU tune cycle (RIG_OP_TUNE via CAT) |
| `radio ptt on\|off` | Set PTT state |
| `messages [--offset N] [--limit N]` | Show decoded message log |
| `messages clear` | Clear message log |
| `spectrum` | Show spectrum snapshot summary |
| `send TEXT [--submode NAME]` | Transmit a message |
| `tx hb` | Send heartbeat (@HB) |
| `tx snr CALL` | Send @SNR? query to callsign |
| `tx info CALL` | Send @INFO? query to callsign |
| `tx grid CALL` | Send @GRID? query to callsign |
| `tx status CALL` | Send @? status query to callsign |
| `tx hearing CALL` | Send @HEARING? query to callsign |
| `tx queue` | Show TX queue contents |
| `tx clear` | Clear TX queue |
| `monitor [--filter EVENT]` | Stream live events (Ctrl-C to stop) |
| `stream [--frames] [--json] [--output FILE]` | Stream decoded messages to stdout or file |

### Examples

```bash
# Check what JF8Call is doing right now
./jf8-tool.py status

# Set your callsign and grid
./jf8-tool.py config set callsign=W5XYZ grid=DM79AA

# View full config (useful for discovering field names)
./jf8-tool.py config get

# Switch to 40m JS8 calling frequency; ATU retunes automatically if autoAtu=true
./jf8-tool.py radio freq 7078.0

# Manually trigger ATU tune cycle (RIG_OP_TUNE via CAT, no RF)
./jf8-tool.py radio tune

# Enable auto-ATU so radio.freq always retuens the ATU on frequency change
./jf8-tool.py config set autoAtu=true

# Tune to 20m, disable heartbeat
./jf8-tool.py radio freq 14078.0
./jf8-tool.py config set heartbeatEnabled=false

# Send a directed message
./jf8-tool.py send "W4ABC DE W5XYZ HELLO 73"

# Send in Turbo mode
./jf8-tool.py send "W4ABC DE W5XYZ QUICK MSG" --submode turbo

# Query a station's SNR
./jf8-tool.py tx snr W4ABC

# Watch all decoded messages in real time
./jf8-tool.py monitor --filter message.decoded

# Watch everything except the 1 Hz status heartbeat and spectrum
./jf8-tool.py monitor

# Connect to a remote JF8Call instance on the LAN
./jf8-tool.py --host 192.168.86.173 status
./jf8-tool.py --host 192.168.86.173 radio freq 14078.0

# Connect rig (IC-7300 via USB, CAT PTT)
./jf8-tool.py radio connect --model 3073 --port /dev/ttyUSB0 --baud 19200 --ptt 1

# Show TX queue, then clear it
./jf8-tool.py tx queue
./jf8-tool.py tx clear

# Disable PSKReporter submission temporarily
./jf8-tool.py config set pskReporterEnabled=false
```

### `config set` type coercion

`jf8-tool.py` automatically coerces values:

- `true` / `false` → Python `bool`
- Integer strings → Python `int`
- Float strings → Python `float`
- Anything else → string

```bash
# Boolean
./jf8-tool.py config set heartbeatEnabled=true

# Integer
./jf8-tool.py config set heartbeatIntervalPeriods=6

# Float
./jf8-tool.py config set frequencyKhz=14078.0 txFreqHz=1500.0

# String
./jf8-tool.py config set stationInfo="QTH: Austin TX  PWR: 100W  ANT: Dipole"

# Multiple fields at once
./jf8-tool.py config set callsign=W5XYZ grid=DM79AA autoReply=true
```

---

## Commands Reference

### `status.get`

Returns the live application state snapshot. The same object is also pushed as a
`status` event at 1 Hz.

```json
{ "type": "cmd", "cmd": "status.get" }
```

**Reply `data`:**

| Field | Type | Description |
|-------|------|-------------|
| `callsign` | string | Station callsign |
| `grid` | string | Maidenhead grid locator |
| `submode` | int | Active submode index |
| `submode_name` | string | Submode name (e.g. "Normal") |
| `frequency_khz` | float | Configured dial frequency in kHz |
| `tx_freq_hz` | float | TX audio offset in Hz |
| `transmitting` | bool | Currently transmitting |
| `audio_running` | bool | Audio input active |
| `radio_connected` | bool | Hamlib rig connected |
| `radio_freq_khz` | float | Rig VFO frequency in kHz (0 = not connected) |
| `radio_mode` | string | Rig mode string (e.g. "USB") |
| `tx_queue_size` | int | Frames pending in TX queue |
| `heartbeat_enabled` | bool | Periodic heartbeat active |
| `heartbeat_interval_periods` | int | Heartbeat every N JS8 periods |
| `auto_reply` | bool | Auto-reply to @SNR?/@INFO? enabled |
| `ws_port` | int | WebSocket port |
| `ws_clients` | int | Number of connected WebSocket clients |

---

### `config.get` / `config.set`

`config.get` returns all persistent settings. `config.set` updates any subset of
them — omit fields you do not want to change.

```json
{ "type": "cmd", "cmd": "config.get" }
```

```json
{
  "type": "cmd",
  "cmd": "config.set",
  "data": {
    "callsign": "W5XYZ",
    "frequencyKhz": 14078.0
  }
}
```

`config.set` saves changes to disk, broadcasts a `config.changed` event to all
clients, and returns the full updated config (same as `config.get`).

**All config fields** (used by both `config.get` and `config.set`):

| Field | Type | Description |
|-------|------|-------------|
| `callsign` | string | Station callsign |
| `grid` | string | Maidenhead grid locator |
| `audioInputName` | string | PortAudio input device name |
| `audioOutputName` | string | PortAudio output device name |
| `modemType` | int | 0=JS8/GFSK8, 1=Codec2 DATAC, 2=Olivia, 3=PSK |
| `submode` | int | Submode index (meaning depends on modemType) |
| `frequencyKhz` | float | Dial frequency display in kHz |
| `txFreqHz` | float | TX audio offset in Hz |
| `txPowerPct` | int | TX power percentage (0–100) |
| `heartbeatEnabled` | bool | Periodic @HB enabled |
| `heartbeatIntervalPeriods` | int | @HB every N JS8 periods |
| `autoReply` | bool | Auto-reply to @SNR?/@INFO? |
| `stationInfo` | string | Info string returned by @INFO? queries |
| `stationStatus` | string | Status string returned by @? queries |
| `cqMessage` | string | CQ message body (appended to callsign in CQ TX) |
| `distMiles` | bool | Show distances in miles (false = km) |
| `autoAtu` | bool | Auto-ATU enabled |
| `pskReporterEnabled` | bool | Submit decoded spots to PSKReporter |
| `rigModel` | int | Hamlib rig model number |
| `rigPort` | string | Serial port or `host:port` for network rigs |
| `rigBaud` | int | Serial baud rate |
| `rigDataBits` | int | Serial data bits (5/6/7/8) |
| `rigStopBits` | int | Serial stop bits (1/2) |
| `rigParity` | int | 0=None, 1=Odd, 2=Even |
| `rigHandshake` | int | 0=None, 1=XON/XOFF, 2=Hardware |
| `rigDtrState` | int | DTR line: 0=Off, 1=On, 2=Unset |
| `rigRtsState` | int | RTS line: 0=Off, 1=On, 2=Unset |
| `pttType` | int | 0=VOX, 1=CAT, 2=DTR, 3=RTS |
| `wsEnabled` | bool | WebSocket API enabled |
| `wsPort` | int | WebSocket port |

**Submode index values:**

| modemType | Index | Name |
|-----------|-------|------|
| 0 (JS8) | 0 | Normal (15 s periods) |
| 0 (JS8) | 1 | Fast (10 s) |
| 0 (JS8) | 2 | Turbo (6 s) |
| 0 (JS8) | 3 | Slow (30 s) |
| 0 (JS8) | 4 | Ultra (4 s) |
| 1 (Codec2) | 0 | DATAC0 |
| 1 (Codec2) | 1 | DATAC1 |
| 1 (Codec2) | 2 | DATAC3 |
| 2 (Olivia) | 0 | 8/500 |
| 2 (Olivia) | 1 | 4/250 |
| 2 (Olivia) | 2 | 8/250 |
| 2 (Olivia) | 3 | 16/500 |
| 2 (Olivia) | 4 | 8/1000 |
| 2 (Olivia) | 5 | 16/1000 |
| 2 (Olivia) | 6 | 32/1000 |
| 3 (PSK) | 0 | BPSK31 |
| 3 (PSK) | 1 | BPSK63 |
| 3 (PSK) | 2 | BPSK125 |
| 3 (PSK) | 3 | PSK63F |
| 3 (PSK) | 4 | PSK125R |
| 3 (PSK) | 5 | PSK250R |
| 3 (PSK) | 6 | PSK500R |

---

### `audio.devices` / `audio.restart`

```json
{ "type": "cmd", "cmd": "audio.devices" }
```

Returns `{ "input": ["Device A", ...], "output": ["Device A", ...] }`.

Use device names from this list as `audioInputName` / `audioOutputName` in
`config.set`. JF8Call persists device names, not indices, and scans for a matching
name on startup — devices can be safely unplugged and replugged.

```json
{ "type": "cmd", "cmd": "audio.restart" }
```

Stops and restarts PortAudio with the current device configuration. Useful after a
device reconnect or if audio has stalled.

---

### Radio Commands

#### `radio.get`

```json
{ "type": "cmd", "cmd": "radio.get" }
```

Returns the current rig connection state and saved serial parameters.

| Field | Type | Description |
|-------|------|-------------|
| `connected` | bool | Rig currently connected |
| `freq_khz` | float | VFO A frequency in kHz (0 if not connected) |
| `mode` | string | Rig mode string (e.g. "USB") |
| `rig_model` | int | Hamlib model number |
| `port` | string | Serial port |
| `baud` | int | Baud rate |
| `data_bits` | int | Data bits |
| `stop_bits` | int | Stop bits |
| `parity` | int | Parity |
| `handshake` | int | Handshake |
| `dtr_state` | int | DTR state |
| `rts_state` | int | RTS state |
| `ptt_type` | int | PTT type |

#### `radio.connect`

```json
{
  "type": "cmd",
  "cmd": "radio.connect",
  "data": {
    "rig_model": 3073,
    "port": "/dev/ttyUSB0",
    "baud": 19200,
    "ptt_type": 1
  }
}
```

All fields optional — omitted fields use the saved rig config. Field names match
`radio.get`. On success a `radio.connected` event is broadcast to all clients.

Common Hamlib model numbers: `3073` = IC-7300, `3085` = IC-7610, `2014` = TS-2000,
`1035` = FT-991A, `361` = K3/KX3. See `hamlib --list` or the Hamlib model list for
the complete catalogue.

#### `radio.disconnect`

```json
{ "type": "cmd", "cmd": "radio.disconnect" }
```

---

### `radio.tune`

```json
{ "type": "cmd", "cmd": "radio.tune" }
```

Sends `RIG_OP_TUNE` to the rig via Hamlib CAT, starting the internal ATU tuning
cycle. Returns an error if the rig is not connected.

This is the clean, no-RF way to trigger ATU tuning on rigs that support
`RIG_OP_TUNE` (IC-7300, IC-7610, and most modern Icom rigs). For rigs that do not
support it, use `radio.ptt.set` to key briefly instead.

When `autoAtu` is `true`, `radio.frequency.set` calls this automatically after
every successful frequency change — so you do not need to call it manually for
normal band-change workflows.

---

### `radio.frequency.set` / `radio.ptt.set`

#### `radio.frequency.set`

```json
{
  "type": "cmd",
  "cmd": "radio.frequency.set",
  "data": { "freq_khz": 14078.0 }
}
```

Sends the frequency to the rig via Hamlib CAT and updates the dial frequency display.
Requires an active rig connection. Returns `{ "freq_khz": 14078.0 }` on success.

#### `radio.ptt.set`

```json
{
  "type": "cmd",
  "cmd": "radio.ptt.set",
  "data": { "ptt": true }
}
```

Direct PTT control, bypassing the TX queue. Use with care — the TX queue manages
PTT automatically during normal operation. Leaving PTT keyed with no audio output
will transmit noise.

---

### `messages.get` / `messages.clear`

```json
{
  "type": "cmd",
  "cmd": "messages.get",
  "data": { "offset": 0, "limit": 50 }
}
```

Both `offset` and `limit` are optional (defaults: 0, 100; max limit: 1000). Messages
are returned newest-first.

**Reply `data`:**

| Field | Type | Description |
|-------|------|-------------|
| `messages` | array | Message objects |
| `count` | int | Number of messages returned |
| `offset` | int | Offset used |
| `limit` | int | Limit used |

**Message object fields:**

| Field | Type | Description |
|-------|------|-------------|
| `time` | string | UTC time `HH:mm:ss` |
| `utc_iso` | string | ISO 8601 UTC timestamp |
| `freq_hz` | float | Audio frequency in Hz |
| `snr_db` | int | Signal-to-noise ratio in dB |
| `submode` | int | Submode index |
| `submode_name` | string | Submode name |
| `from` | string | Sender callsign |
| `to` | string | Destination callsign (empty for broadcasts) |
| `body` | string | Parsed message body |
| `raw` | string | Full raw decoded text |
| `type` | int | Message type (see below) |
| `type_name` | string | Message type name |

**Message type values:**

| Value | Name | Description |
|-------|------|-------------|
| 0 | Unknown | Unrecognised format |
| 1 | Heartbeat | `CALLSIGN @HB` |
| 2 | DirectedMessage | Generic directed text |
| 3 | SnrQuery | `@SNR?` |
| 4 | SnrReply | SNR report |
| 5 | InfoQuery | `@INFO?` |
| 6 | InfoReply | Info reply |
| 7 | StatusQuery | `@?` |
| 8 | StatusReply | Status reply |
| 9 | CompoundDirected | Multi-part directed message |

```json
{ "type": "cmd", "cmd": "messages.clear" }
```

---

### `spectrum.get`

```json
{ "type": "cmd", "cmd": "spectrum.get" }
```

Returns a snapshot of the most recent FFT output.

| Field | Type | Description |
|-------|------|-------------|
| `bins` | array of float | FFT magnitude bins in dBFS |
| `bin_count` | int | Number of bins (2048) |
| `hz_per_bin` | float | Frequency resolution per bin |
| `sample_rate` | float | Sample rate of the decimated input (12000 Hz) |

The spectrum spans 0 to 6000 Hz (the Nyquist frequency of the 12 kHz decimated
input). Bin *n* corresponds to `n * hz_per_bin` Hz. The same data is also pushed
as a `spectrum` event at ~5 Hz while audio is running.

---

### `tx.send`

Encodes and queues a message for transmission at the next period boundary.

```json
{
  "type": "cmd",
  "cmd": "tx.send",
  "data": {
    "text": "W4ABC DE W5XYZ HELLO 73",
    "submode": "normal"
  }
}
```

`text` is required. `submode` is optional; if omitted, the currently active submode
is used.

**Accepted `submode` values (JS8 / GFSK8):**

| Value | Submode |
|-------|---------|
| `"normal"` or `"a"` or `0` | Normal (15 s) |
| `"fast"` or `"b"` or `1` | Fast (10 s) |
| `"turbo"` or `"c"` or `2` | Turbo (6 s) |
| `"slow"` or `"e"` or `3` | Slow (30 s) |
| `"ultra"` or `"i"` or `4` | Ultra (4 s) |

For Codec2, use `"datac0"`, `"datac1"`, `"datac3"` or numeric indices 0–2.

**Reply `data`:**

| Field | Type | Description |
|-------|------|-------------|
| `queue_size` | int | Total frames now in TX queue |

After the reply, a `tx.queued` event is broadcast. When TX actually starts at the
next period boundary, `tx.started` is broadcast; when complete, `tx.finished`.

---

### `tx.queue.get` / `tx.queue.clear`

```json
{ "type": "cmd", "cmd": "tx.queue.get" }
```

Returns:
```json
{
  "queue": [
    { "payload": "...", "frame_type": 1, "submode": 0 },
    ...
  ],
  "queue_size": 2
}
```

`frame_type` values correspond to `Varicode::TransmissionType`: `0`=middle frame,
`1`=first frame, `2`=last frame, `3`=single-frame directed message.

```json
{ "type": "cmd", "cmd": "tx.queue.clear" }
```

Removes all pending frames. Does **not** abort a frame currently being transmitted.

---

### Protocol TX Commands

These commands format and queue standard JS8 protocol messages.

#### `tx.hb` — Transmit heartbeat

```json
{ "type": "cmd", "cmd": "tx.hb" }
```

Queues `CALLSIGN @HB`. Requires callsign to be configured.

#### `tx.snr` — Request SNR report

```json
{ "type": "cmd", "cmd": "tx.snr", "data": { "to": "W4ABC" } }
```

Queues `W4ABC MYCALL @SNR?`. The recipient will reply with their measured SNR of
your signal if auto-reply is enabled on their end.

#### `tx.info` — Request station info

```json
{ "type": "cmd", "cmd": "tx.info", "data": { "to": "W4ABC" } }
```

Queues `W4ABC MYCALL @INFO?`.

#### `tx.status` — Request status

```json
{ "type": "cmd", "cmd": "tx.status", "data": { "to": "W4ABC" } }
```

Queues `W4ABC MYCALL @?`.

All protocol TX commands require `to` and a configured callsign; both return
`{ "queue_size": N }`.

---

## Events Reference

All events use the envelope:
```json
{ "type": "event", "event": "event.name", "data": { ... } }
```

### `status` (1 Hz)

Full status snapshot. Same fields as `status.get` reply. Use this for
polling-style clients; event-driven clients can ignore it and rely on specific
change events.

### `message.frame`

Emitted for each individual GFSK8 frame of a **multi-frame** message as it
arrives, before assembly is complete. Allows clients to show in-progress messages
in real time.

Not emitted for single-frame messages (those go straight to `message.decoded`).
Not emitted for streaming modems — PSK/Olivia/Codec2 already emit `message.decoded`
for each decoded chunk as it arrives.

Key fields: `freq_hz`, `snr_db`, `submode`, `frame_type` (1=first, 0=middle),
`frame_text` (this frame only), `assembled_text` (cumulative text so far),
`is_complete` (always false).

Group frames from the same transmission by `round(freq_hz / 10)`.
The assembled result arrives as `message.decoded`.

### `message.decoded`

Emitted when the decoder produces a complete message. For multi-frame GFSK8
messages, fires once after the final frame is received and assembled. For
streaming modems, fires for each decoded text chunk.

```json
{
  "time": "14:23:00",
  "utc_iso": "2026-03-25T14:23:00Z",
  "freq_hz": 1523.4,
  "snr_db": -10,
  "submode": 0,
  "submode_name": "Normal",
  "from": "W4ABC",
  "to": "W5XYZ",
  "body": "HELLO 73",
  "raw": "W4ABC W5XYZ HELLO 73",
  "type": 2,
  "type_name": "DirectedMessage"
}
```

### `spectrum` (~5 Hz)

FFT spectrum data. Same fields as `spectrum.get`. Throttled to ~5 Hz regardless
of the internal FFT rate. Contains no `data` field when audio is not running.

### `tx.started` / `tx.finished`

No `data`. Bracket the actual on-air transmission (PTT asserted → PTT released).

### `tx.queued`

```json
{ "queue_size": 3 }
```

Fired whenever a new message is added to the TX queue.

### `radio.connected`

```json
{ "freq_khz": 14078.0, "mode": "USB" }
```

### `radio.disconnected`

No `data`.

### `config.changed`

Fired whenever any config field changes, whether via GUI or API.

```json
{
  "callsign": "W5XYZ",
  "grid": "DM79AA",
  "modem_type": 0,
  "submode": 0,
  "frequency_khz": 14078.0,
  "tx_freq_hz": 1500.0,
  "heartbeat_enabled": true,
  "heartbeat_interval_periods": 4,
  "auto_reply": true
}
```

Note: `config.changed` uses snake_case keys. `config.get` / `config.set` use
camelCase. Both name sets refer to the same underlying fields.

### `error`

```json
{ "message": "Audio error: device not found" }
```

---

## Python Integration Examples

### Minimal async client

```python
import asyncio, json, uuid
import websockets

async def send_cmd(ws, cmd, data=None):
    msg = {"type": "cmd", "id": str(uuid.uuid4())[:8], "cmd": cmd}
    if data:
        msg["data"] = data
    await ws.send(json.dumps(msg))
    while True:
        obj = json.loads(await ws.recv())
        if obj.get("type") == "reply" and obj.get("id") == msg["id"]:
            return obj

async def main():
    async with websockets.connect("ws://localhost:2102") as ws:
        await ws.recv()  # hello
        await ws.recv()  # initial status push

        # Query status
        r = await send_cmd(ws, "status.get")
        print(r["data"]["callsign"], r["data"]["frequency_khz"], "kHz")

        # Tune to 40m
        r = await send_cmd(ws, "radio.frequency.set", {"freq_khz": 7078.0})
        print("Tuned:", r)

asyncio.run(main())
```

### Event-driven decoder logger

```python
import asyncio, json
import websockets

async def log_decodes():
    async with websockets.connect("ws://localhost:2102") as ws:
        await ws.recv()  # hello
        await ws.recv()  # status

        print("Listening for decoded messages…")
        async for raw in ws:
            msg = json.loads(raw)
            if msg.get("type") == "event" and msg["event"] == "message.decoded":
                d = msg["data"]
                print(f"[{d['time']}] {d['from']} → {d['to'] or '*'}: {d['body']}")

asyncio.run(log_decodes())
```

### Multi-client safe config update

Because `config.set` broadcasts `config.changed` to all clients, other clients
always stay in sync without polling:

```python
# Client A sets callsign
await send_cmd(ws_a, "config.set", {"callsign": "W5XYZ"})

# Client B (connected independently) receives without polling:
# {"type":"event","event":"config.changed","data":{"callsign":"W5XYZ",...}}
```

### Transmit and wait for TX complete

```python
async def transmit_and_wait(ws, text):
    tx_done = asyncio.Event()

    # Send the message
    r = await send_cmd(ws, "tx.send", {"text": text})
    if not r["ok"]:
        raise RuntimeError(r["error"])

    # Now wait for the tx.finished event
    async for raw in ws:
        ev = json.loads(raw)
        if ev.get("type") == "event" and ev["event"] == "tx.finished":
            tx_done.set()
            break

    return tx_done.is_set()
```

### Watching the spectrum

```python
async def peak_frequency(ws):
    """Return the frequency (Hz) of the loudest signal in the current spectrum."""
    r = await send_cmd(ws, "spectrum.get")
    bins = r["data"]["bins"]
    hz_per_bin = r["data"]["hz_per_bin"]
    peak = max(range(len(bins)), key=lambda i: bins[i])
    return peak * hz_per_bin
```

### Setting station info for @INFO? auto-replies

```python
await send_cmd(ws, "config.set", {
    "stationInfo": "QTH: Austin TX  PWR: 100W  ANT: 80m OCF Dipole",
    "stationStatus": "Available for sked",
    "autoReply": True,
})
```

---

## Design Notes

### GUI / API parity

Every feature accessible in the JF8Call GUI is also accessible via the WebSocket
API, and vice versa. This is a hard architectural requirement. If you find a GUI
capability without a corresponding API command, report it as a bug.

### Thread safety

All API command handlers run on the Qt main thread. It is safe to call any API
method from any connected client; commands are serialized through Qt's event loop.

### Frequency nomenclature

Dial frequencies are always in **kHz** throughout the API (`frequencyKhz`,
`freq_khz`, `radio_freq_khz`). The TX audio offset is in **Hz** (`txFreqHz`,
`tx_freq_hz`). The spectrum bins are in Hz.

### JS8 multi-frame assembly

JS8/GFSK8 messages can span multiple 15-second (or shorter) periods. The
`message.decoded` event is not fired until the final frame is received and the
complete message is assembled. Partial frame arrivals update the "Heard" pane in
the GUI in real time, but do not generate WS events. Expect up to 4× the nominal
period duration between signal start and `message.decoded` for a long multi-frame
message.

### PSKReporter integration

When `pskReporterEnabled` is true (the default), JF8Call submits every decoded
station as a spot to `report.pskreporter.info` using the IPFIX/UDP protocol.
Spots are batched and sent every 2 minutes. To suppress submission (e.g. during
testing):

```bash
./jf8-tool.py config set pskReporterEnabled=false
```

### Connection lifecycle

The WebSocket server does not implement authentication or rate limiting. It is
bound to `127.0.0.1` by default and should not be exposed to untrusted networks
without an additional access control layer.

If JF8Call is restarted, clients must reconnect. There is no automatic
reconnection in `jf8-tool.py`; long-running integrations should implement their
own reconnection loop with backoff.
