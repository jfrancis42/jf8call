# JF8Call

A clean-room, JS8Call-compatible station application for Linux, Windows, and Raspberry Pi.

JF8Call implements the JS8 digital radio protocol — a weak-signal HF keyboard-to-keyboard mode — with a strict separation between the modem back-end and user interface, and a pluggable modem architecture that supports JS8, Olivia MFSK, PSK31/63/FEC, and Codec2 FreeDV alongside the native JS8 mode. The name follows the same convention as JS8Call itself: JS8Call was named after Jordan Sherer's initials (JS), and JF8Call is named after the initials of its author, Jeff Francis (JF), N0GQ.

**Status: Early Alpha.** The application works and is actively used, but APIs and behavior may change without notice. Feedback and bug reports are welcome.

---

## Features

- **JS8 (all five submodes):** Normal (15 s), Fast (10 s), Turbo (6 s), Slow (30 s), Ultra (4 s); full interoperability with JS8Call
- **Olivia MFSK:** FEC-protected weak-signal HF mode; 8/500, 4/250, 8/250, 16/500, 8/1000, 16/1000, 32/1000
- **PSK31/63/125 and PSK FEC:** BPSK31, BPSK63, BPSK125, PSK63F, PSK125R, PSK250R, PSK500R
- **Codec2 FreeDV:** DATAC0 (500 Hz / 274 bps), DATAC1 (1.6 kHz / 980 bps), DATAC3 (2.4 kHz / 2.2 kbps) — Linux and Windows
- Transmits with UTC period-synchronized timing (JS8) or immediately (streaming modes)
- Waterfall display with scrolling spectrogram
- Hamlib radio control (frequency, PTT) — optional
- WebSocket API on `ws://localhost:2102` for headless operation and external integrations
- `@HB` heartbeat, `@SNR?`, `@INFO?`, and directed message protocols (JS8 mode)
- Headless operation with `--headless` flag (all audio, decode, and WebSocket API active, no display required)

---

## Modem libraries

JF8Call uses several optional modem libraries alongside its core JS8 modem:

| Library | Provides | Default path |
|---------|----------|--------------|
| [gfsk8-modem-clean](https://github.com/jfrancis42/gfsk8-modem-clean) | JS8 / GFSK8 (required) | `~/gfsk8-modem-clean` |
| [olivia-modem](https://github.com/jfrancis42/olivia-modem) | Olivia MFSK (optional) | `~/olivia-modem` |
| [libpsk](https://github.com/jfrancis42/libpsk) | PSK31/63/125/FEC (optional) | `~/psk31` |
| system `libcodec2` | FreeDV DATAC (optional) | detected via pkg-config |

CMake detects each library automatically if found at the default path. All modems are optional except gfsk8-modem-clean.

---

## Dependencies

### Linux / Raspberry Pi

```bash
sudo apt-get install qt6-base-dev libqt6websockets6-dev libhamlib-dev libportaudio-dev \
    cmake ninja-build
# Optional — for Codec2 FreeDV modes:
sudo apt-get install libcodec2-dev
```

### Clone modem libraries

```bash
git clone https://github.com/jfrancis42/gfsk8-modem-clean ~/gfsk8-modem-clean
git clone https://github.com/jfrancis42/olivia-modem ~/olivia-modem
git clone https://github.com/jfrancis42/libpsk ~/psk31
git clone https://github.com/jfrancis42/jf8call
```

---

## Building

### Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
# Binary: build/jf8call
```

CMake will automatically locate gfsk8-modem-clean, olivia-modem, and libpsk at their
default paths (`~/gfsk8-modem-clean`, `~/olivia-modem`, `~/psk31`). Pass
`-DGFSK8MODEM_DIR=<path>`, `-DOLIVIA_MODEM_DIR=<path>`, or `-DPSK_MODEM_DIR=<path>`
to override.

### Linux AppImage

```bash
./package-appimage.sh
# Output: JF8Call-0.4.0-ALPHA-x86_64.AppImage
```

### Windows (cross-compile from Linux)

```bash
./build-windows-cross.sh --setup   # one-time: downloads Qt6 + Hamlib to ~/Qt-cross/
./build-windows-cross.sh           # builds jf8call.exe + DLLs in build-windows/deploy/
```

The Windows build cross-compiles gfsk8-modem-clean, olivia-modem, and libpsk for
the MinGW target automatically.

### Raspberry Pi (aarch64 cross-compile)

```bash
./build-raspberrypi.sh --setup     # one-time: installs arm64 packages
./build-raspberrypi.sh             # builds build-rpi/jf8call (ELF aarch64)
```

The RPi build cross-compiles gfsk8-modem-clean, olivia-modem, and libpsk for
aarch64 automatically. Codec2 is not included (no arm64 system package available).

---

## WebSocket API

JF8Call exposes a full WebSocket API on `ws://localhost:2102` that provides
complete parity with the GUI. Every operation available in the GUI — including
modem selection and submode changes — can also be performed via the API. See
[websocket-api.md](websocket-api.md) for the full reference.

---

## JS8 Submode Parameters

| Submode | Period | Tone spacing | SNR threshold |
|---------|--------|--------------|---------------|
| Normal (JS8A) | 15 s | 6.25 Hz | −24 dB |
| Fast (JS8B) | 10 s | 10.0 Hz | −22 dB |
| Turbo (JS8C) | 6 s | 20.0 Hz | −20 dB |
| Slow (JS8E) | 30 s | 3.125 Hz | −28 dB |
| Ultra (JS8I) | 4 s | 31.25 Hz | −18 dB |

---

## License

GPL-3.0. JF8Call and all modem libraries it links are GPL-3.0.

The JS8 protocol was created by Jordan Sherer KN4CRD. The C++ modem implementation
was originally developed by Allan Bazinet W6BAZ and contributors to JS8Call-improved.
The Olivia DSP core is by Pawel Jalocha SP9VRC (via fldigi). libpsk is original work.
JF8Call is a new application built on top of these libraries.
