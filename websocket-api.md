# JF8Call WebSocket API

## Overview

JF8Call exposes a WebSocket API on `ws://localhost:2102` (port configurable in Preferences). Every message is a UTF-8 JSON text frame. The API gives 100% programmatic access to all functionality available in the GUI.

The server is enabled by default. Disable it via **Preferences → WebSocket API** or by setting `wsEnabled: false` in `~/.jf8call/settings.json`.

---

## Message Framing

### Client → Server (commands)

```json
{
  "type": "cmd",
  "id": "<opaque string — optional>",
  "cmd": "<command name>",
  "data": { ... }
}
```

The `id` field is echoed back in the reply for correlation. Omit it for fire-and-forget commands.

### Server → Client (replies)

```json
{ "type": "reply", "id": "<same>", "ok": true,  "data": { ... } }
{ "type": "reply", "id": "<same>", "ok": false, "error": "message" }
```

### Server → Client (events — unsolicited)

```json
{ "type": "event", "event": "<event name>", "data": { ... } }
```

Events are pushed to **all connected clients** automatically when application state changes. No subscription is needed.

---

## Connection

On connect, the server immediately sends two messages:

1. A `hello` message:
```json
{ "type": "hello", "data": { "message": "JF8Call WebSocket API", "version": "1.0" } }
```

2. A full `status` event with current application state.

---

## Commands

### `status.get` — Get current status

```json
{ "type": "cmd", "cmd": "status.get" }
```

Reply `data`:

| Field | Type | Description |
|-------|------|-------------|
| `callsign` | string | Station callsign |
| `grid` | string | Maidenhead grid locator |
| `submode` | int | Submode index (0=Normal, 1=Fast, 2=Turbo, 3=Slow, 4=Ultra) |
| `submode_name` | string | Human-readable submode name |
| `frequency_khz` | float | Configured dial frequency in kHz |
| `tx_freq_hz` | float | TX audio frequency offset in Hz |
| `transmitting` | bool | Currently transmitting |
| `audio_running` | bool | Audio input active |
| `radio_connected` | bool | Hamlib rig connected |
| `radio_freq_khz` | float | Rig VFO frequency in kHz (0 if not connected) |
| `radio_mode` | string | Rig mode string (e.g. "USB") |
| `tx_queue_size` | int | Frames pending in TX queue |
| `heartbeat_enabled` | bool | Periodic heartbeat active |
| `heartbeat_interval_periods` | int | Heartbeat every N JS8 periods |
| `auto_reply` | bool | Auto-reply to @SNR?/@INFO? enabled |
| `ws_port` | int | WebSocket port |
| `ws_clients` | int | Number of connected WebSocket clients |

---

### `config.get` — Get full configuration

```json
{ "type": "cmd", "cmd": "config.get" }
```

Reply `data` — all persistent config fields:

| Field | Type | Description |
|-------|------|-------------|
| `callsign` | string | Station callsign |
| `grid` | string | Maidenhead grid locator |
| `audioInputName` | string | PortAudio input device name |
| `audioOutputName` | string | PortAudio output device name |
| `modemType` | int | Active modem (0=JS8/GFSK8, 1=Codec2, 2=Olivia, 3=PSK) |
| `submode` | int | Active submode index |
| `frequencyKhz` | float | Dial frequency in kHz |
| `txFreqHz` | float | TX audio offset in Hz |
| `txPowerPct` | int | TX power percentage (0–100) |
| `heartbeatEnabled` | bool | Heartbeat enabled |
| `heartbeatIntervalPeriods` | int | Heartbeat interval |
| `autoReply` | bool | Auto-reply enabled |
| `stationInfo` | string | Station info string (sent in @INFO? replies) |
| `stationStatus` | string | Station status string (sent in @? replies) |
| `cqMessage` | string | CQ message body |
| `distMiles` | bool | Display distances in miles (false = km) |
| `autoAtu` | bool | Auto-ATU enabled |
| `pskReporterEnabled` | bool | Submit spots to PSKReporter |
| `rigModel` | int | Hamlib rig model number |
| `rigPort` | string | Hamlib port (e.g. `/dev/ttyUSB0`) |
| `rigBaud` | int | Hamlib serial baud rate |
| `rigDataBits` | int | Serial data bits (5/6/7/8) |
| `rigStopBits` | int | Serial stop bits (1/2) |
| `rigParity` | int | Serial parity (0=None, 1=Odd, 2=Even) |
| `rigHandshake` | int | Serial handshake (0=None, 1=XONXOFF, 2=Hardware) |
| `rigDtrState` | int | DTR line state (0=Off, 1=On, 2=Unset) |
| `rigRtsState` | int | RTS line state (0=Off, 1=On, 2=Unset) |
| `pttType` | int | PTT type (0=VOX, 1=CAT, 2=DTR, 3=RTS) |
| `wsEnabled` | bool | WebSocket API enabled |
| `wsPort` | int | WebSocket port |

---

### `config.set` — Update configuration

All fields are optional — supply only the fields you want to change. Changes are saved to disk and a `config.changed` event is broadcast to all clients.

Settable fields (use the same key names as `config.get`):

| Field | Type | Notes |
|-------|------|-------|
| `callsign` | string | |
| `grid` | string | |
| `audioInputName` | string | Audio restarted on change |
| `audioOutputName` | string | Audio restarted on change |
| `modemType` | int | 0=JS8/GFSK8, 1=Codec2, 2=Olivia, 3=PSK |
| `submode` | int | Resets to modem default when modemType changes |
| `frequencyKhz` | float | Dial frequency display only (no CAT) |
| `txFreqHz` | float | |
| `heartbeatEnabled` | bool | |
| `heartbeatIntervalPeriods` | int | |
| `autoReply` | bool | |
| `stationInfo` | string | Returned in @INFO? replies |
| `stationStatus` | string | Returned in @? replies |
| `cqMessage` | string | |
| `distMiles` | bool | |
| `autoAtu` | bool | |
| `pskReporterEnabled` | bool | |
| `rigModel` | int | Hamlib model number |
| `rigPort` | string | |
| `rigBaud` | int | |
| `rigDataBits` | int | |
| `rigStopBits` | int | |
| `rigParity` | int | 0=None, 1=Odd, 2=Even |
| `rigHandshake` | int | 0=None, 1=XONXOFF, 2=Hardware |
| `rigDtrState` | int | 0=Off, 1=On, 2=Unset |
| `rigRtsState` | int | 0=Off, 1=On, 2=Unset |
| `pttType` | int | 0=VOX, 1=CAT, 2=DTR, 3=RTS |

PSK `submode` values: `0`=BPSK31, `1`=BPSK63 (default), `2`=BPSK125, `3`=PSK63F, `4`=PSK125R, `5`=PSK250R, `6`=PSK500R.

Olivia `submode` values: `0`=8/500 (default), `1`=4/250, `2`=8/250, `3`=16/500, `4`=8/1000, `5`=16/1000, `6`=32/1000.

Example:
```json
{
  "type": "cmd",
  "cmd": "config.set",
  "data": {
    "callsign": "W5XYZ",
    "grid": "DM79AA",
    "frequencyKhz": 14078.0,
    "heartbeatEnabled": true
  }
}
```

---

### `audio.devices` — List available audio devices

```json
{ "type": "cmd", "cmd": "audio.devices" }
```

Reply `data`:

| Field | Type | Description |
|-------|------|-------------|
| `input` | array of string | Available input device names |
| `output` | array of string | Available output device names |

---

### `audio.restart` — Restart audio I/O

```json
{ "type": "cmd", "cmd": "audio.restart" }
```

Stops and restarts PortAudio with the currently configured devices.

---

### `radio.get` — Get radio status

```json
{ "type": "cmd", "cmd": "radio.get" }
```

Reply `data`:

| Field | Type | Description |
|-------|------|-------------|
| `connected` | bool | Rig connected |
| `freq_khz` | float | Current VFO frequency in kHz |
| `mode` | string | Current rig mode |
| `rig_model` | int | Hamlib model number |
| `port` | string | Serial port |
| `baud` | int | Baud rate |
| `data_bits` | int | Serial data bits |
| `stop_bits` | int | Serial stop bits |
| `parity` | int | Serial parity |
| `handshake` | int | Serial handshake |
| `dtr_state` | int | DTR line state |
| `rts_state` | int | RTS line state |
| `ptt_type` | int | PTT type (0=VOX, 1=CAT, 2=DTR, 3=RTS) |

---

### `radio.connect` — Connect to rig

```json
{
  "type": "cmd",
  "cmd": "radio.connect",
  "data": {
    "rig_model": 3073,
    "port": "/dev/ttyUSB0",
    "baud": 9600,
    "ptt_type": 1
  }
}
```

All fields optional; omitted fields use saved config. Field names match those returned by `radio.get`. On success, a `radio.connected` event is broadcast.

---

### `radio.disconnect` — Disconnect from rig

```json
{ "type": "cmd", "cmd": "radio.disconnect" }
```

---

### `radio.frequency.set` — Set VFO frequency

```json
{
  "type": "cmd",
  "cmd": "radio.frequency.set",
  "data": { "freq_khz": 14078.0 }
}
```

Requires a connected rig. Sends `freq_khz` to the radio via Hamlib and updates the dial frequency display.

---

### `radio.tune` — Trigger ATU tune cycle

```json
{ "type": "cmd", "cmd": "radio.tune" }
```

Sends `RIG_OP_TUNE` to the rig via Hamlib CAT, triggering the internal ATU tuning
cycle without transmitting noise. Requires a connected rig. Returns an error if
the rig is not connected.

Not all rigs support `RIG_OP_TUNE` via CAT — consult the Hamlib rig capabilities
for your model. For rigs that do not, key PTT briefly with `radio.ptt.set` instead.

When `autoAtu` is enabled (see `config.set`), `radio.frequency.set` automatically
calls `radio.tune` after successfully changing the VFO frequency — no manual call
needed.

---

### `radio.ptt.set` — Control PTT

```json
{
  "type": "cmd",
  "cmd": "radio.ptt.set",
  "data": { "ptt": true }
}
```

Direct PTT control. Use with caution — the TX queue manages PTT automatically during normal operation.

---

### `messages.get` — Get decoded message log

```json
{
  "type": "cmd",
  "cmd": "messages.get",
  "data": { "offset": 0, "limit": 100 }
}
```

Both `offset` and `limit` are optional (defaults: 0, 100). Messages are newest-first.

Reply `data`:

| Field | Type | Description |
|-------|------|-------------|
| `messages` | array | Array of message objects (see below) |
| `count` | int | Number of messages returned |
| `offset` | int | Offset used |
| `limit` | int | Limit used |

Message object fields:

| Field | Type | Description |
|-------|------|-------------|
| `time` | string | UTC time `HH:mm:ss` |
| `utc_iso` | string | Full ISO 8601 UTC timestamp |
| `freq_hz` | float | Audio frequency in Hz |
| `snr_db` | int | SNR in dB |
| `submode` | string | Submode name |
| `from` | string | Source callsign |
| `to` | string | Destination callsign (or empty for broadcasts) |
| `body` | string | Message body |
| `raw` | string | Full raw decoded text |

---

### `messages.clear` — Clear message log

```json
{ "type": "cmd", "cmd": "messages.clear" }
```

---

### `spectrum.get` — Get latest spectrum snapshot

```json
{ "type": "cmd", "cmd": "spectrum.get" }
```

Reply `data`:

| Field | Type | Description |
|-------|------|-------------|
| `bins` | array of float | FFT magnitude bins (dB, 2048 bins) |
| `bin_count` | int | Number of bins |
| `hz_per_bin` | float | Frequency resolution |
| `sample_rate` | float | Sample rate in Hz (12000) |

---

### `tx.send` — Transmit a message

```json
{
  "type": "cmd",
  "cmd": "tx.send",
  "data": {
    "text": "W4ABC HELLO",
    "submode": "normal"
  }
}
```

`text` is required. `submode` is optional (default: current active submode). Accepted submode values: `"normal"` / `"a"` / `0`, `"fast"` / `"b"` / `1`, `"turbo"` / `"c"` / `2`, `"slow"` / `"e"` / `3`, `"ultra"` / `"i"` / `4`.

The message is encoded via `js8::pack()` and enqueued. A `tx.queued` event is broadcast.

Reply `data`:

| Field | Type | Description |
|-------|------|-------------|
| `queue_size` | int | Total frames now in queue |

---

### `tx.queue.get` — Get TX queue

```json
{ "type": "cmd", "cmd": "tx.queue.get" }
```

Reply `data`:

| Field | Type | Description |
|-------|------|-------------|
| `queue` | array | Array of frame objects |
| `size` | int | Number of queued frames |

Frame object: `{ "payload": "...", "frame_type": 0, "submode": 0 }`

---

### `tx.queue.clear` — Clear TX queue

```json
{ "type": "cmd", "cmd": "tx.queue.clear" }
```

Removes all pending TX frames. Does not abort a transmission already in progress.

---

### `tx.hb` — Transmit heartbeat

```json
{ "type": "cmd", "cmd": "tx.hb" }
```

Queues a `CALLSIGN @HB` heartbeat frame using the configured callsign.

---

### `tx.snr` — Send @SNR? query

```json
{
  "type": "cmd",
  "cmd": "tx.snr",
  "data": { "to": "W4ABC" }
}
```

Queues a `W4ABC MYCALL @SNR?` message asking the destination to report SNR.

---

### `tx.info` — Send @INFO? query

```json
{
  "type": "cmd",
  "cmd": "tx.info",
  "data": { "to": "W4ABC" }
}
```

Queues a `W4ABC MYCALL @INFO?` message asking for station info.

---

### `tx.status` — Send @? status query

```json
{
  "type": "cmd",
  "cmd": "tx.status",
  "data": { "to": "W4ABC" }
}
```

Queues a `W4ABC MYCALL @?` directed status query.

---

## Events (server-push)

Events are sent automatically to all connected clients. No subscription is required.

### `status` — Full status snapshot (1 Hz)

Sent every second. Same fields as `status.get` reply.

```json
{
  "type": "event",
  "event": "status",
  "data": {
    "callsign": "W5XYZ",
    "submode": 0,
    "transmitting": false,
    ...
  }
}
```

### `message.decoded` — New decoded JS8 message

Sent whenever the decoder produces output.

```json
{
  "type": "event",
  "event": "message.decoded",
  "data": {
    "time": "14:23:00",
    "utc_iso": "2026-03-25T14:23:00Z",
    "freq_hz": 1523.4,
    "snr_db": -10,
    "submode": 0,
    "submode_name": "Normal",
    "from": "W4ABC",
    "to": "W5XYZ",
    "body": "HELLO",
    "raw": "W4ABC W5XYZ HELLO",
    "type": 1,
    "type_name": "DirectedMessage"
  }
}
```

Message type values: `Unknown=0`, `Heartbeat=1`, `DirectedMessage=2`, `SnrQuery=3`, `SnrReply=4`, `InfoQuery=5`, `InfoReply=6`, `StatusQuery=7`, `StatusReply=8`, `CompoundDirected=9`.

### `spectrum` — Spectrum update (~5 Hz)

```json
{
  "type": "event",
  "event": "spectrum",
  "data": {
    "bins": [0.0, 0.1, ...],
    "bin_count": 2048,
    "hz_per_bin": 2.93,
    "sample_rate": 12000.0
  }
}
```

Throttled to ~5 Hz regardless of the audio FFT rate.

### `tx.started` — TX begun

```json
{ "type": "event", "event": "tx.started" }
```

### `tx.finished` — TX complete

```json
{ "type": "event", "event": "tx.finished" }
```

### `tx.queued` — Frame added to TX queue

```json
{ "type": "event", "event": "tx.queued", "data": { "queue_size": 2 } }
```

### `radio.connected` — Rig connected

```json
{
  "type": "event",
  "event": "radio.connected",
  "data": { "freq_khz": 14078.0, "mode": "USB" }
}
```

### `radio.disconnected` — Rig disconnected

```json
{ "type": "event", "event": "radio.disconnected" }
```

### `config.changed` — Configuration updated

Sent whenever any config field changes (via GUI or API).

```json
{
  "type": "event",
  "event": "config.changed",
  "data": {
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
}
```

### `error` — Application error

```json
{ "type": "event", "event": "error", "data": { "message": "Audio error: ..." } }
```

---

## Example Session

```
# Connect: ws://localhost:2102

# ← Server sends immediately:
{"type":"hello","data":{"message":"JF8Call WebSocket API","version":"1.0"}}
{"type":"event","event":"status","data":{"callsign":"W5XYZ","submode":0,...}}

# → Client queries config:
{"type":"cmd","id":"1","cmd":"config.get"}

# ← Server replies:
{"type":"reply","id":"1","ok":true,"data":{"callsign":"W5XYZ","grid":"DM79AA",...}}

# → Client sets callsign:
{"type":"cmd","id":"2","cmd":"config.set","data":{"callsign":"N0CALL"}}

# ← Server replies and broadcasts config.changed to all clients:
{"type":"reply","id":"2","ok":true,"data":{}}
{"type":"event","event":"config.changed","data":{"callsign":"N0CALL",...}}

# → Client transmits a message:
{"type":"cmd","id":"3","cmd":"tx.send","data":{"text":"W4ABC HELLO"}}

# ← Server replies, then broadcasts events as TX proceeds:
{"type":"reply","id":"3","ok":true,"data":{"frames":1,"queue_size":1}}
{"type":"event","event":"tx.queued","data":{"queue_size":1}}
{"type":"event","event":"tx.started"}
{"type":"event","event":"tx.finished"}

# ← When a JS8 message is decoded:
{"type":"event","event":"message.decoded","data":{"from":"W4ABC","to":"W5XYZ","body":"HELLO",...}}
```
