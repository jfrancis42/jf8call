# JF8Call

A clean-room, JS8Call-compatible station application for Linux, Windows, and Raspberry Pi.

JF8Call implements the JS8 digital radio protocol — a weak-signal HF keyboard-to-keyboard mode — with a strict separation between the modem back-end and user interface. The name follows the same convention as JS8Call itself: JS8Call was named after Jordan Sherer's initials (JS), and JF8Call is named after the initials of its author, Jeff Francis (JF), N0GQ.

**Status: Early Alpha.** The application works and is actively used, but APIs and behavior may change without notice. Feedback and bug reports are welcome.

---

## Features

- Receives and decodes all five JS8 submodes: Normal (15 s), Fast (10 s), Turbo (6 s), Slow (30 s), Ultra (4 s)
- Transmits with UTC period-synchronized timing
- Waterfall display with scrolling spectrogram
- Hamlib radio control (frequency, PTT) — optional
- WebSocket API on `ws://localhost:2102` for headless operation and external integrations
- `@HB` heartbeat, `@SNR?`, `@INFO?`, and directed message protocols
- Interoperable with JS8Call and other JS8 software

---

## Dependencies

### Linux / Raspberry Pi

```
qt6-base-dev libqt6websockets6-dev libhamlib-dev libportaudio-dev cmake ninja-build
```

```bash
sudo apt-get install qt6-base-dev libqt6websockets6-dev libhamlib-dev libportaudio-dev cmake ninja-build
```

### gfsk8-modem-clean

JF8Call uses [gfsk8-modem-clean](https://github.com/jfrancis42/gfsk8-modem-clean) as its modem library. Clone it alongside this repository:

```bash
git clone https://github.com/jfrancis42/gfsk8-modem-clean ~/gfsk8-modem-clean
git clone https://github.com/jfrancis42/jf8call
```

---

## Building

### Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGFSK8MODEM_DIR=~/gfsk8-modem-clean
cmake --build build -j2
# Binary: build/jf8call
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

### Raspberry Pi (aarch64 cross-compile)

```bash
./build-raspberrypi.sh --setup     # one-time: installs arm64 packages
./build-raspberrypi.sh             # builds build-rpi/jf8call (ELF aarch64)
```

---

## WebSocket API

JF8Call exposes a full WebSocket API on `ws://localhost:2102` that provides
complete parity with the GUI. Every operation available in the GUI can also be
performed via the API. See [websocket-api.md](websocket-api.md) for the full
reference. The included `api-tool.py` demonstrates the API from the command line.

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

GPL-3.0. JF8Call links [gfsk8-modem-clean](https://github.com/jfrancis42/gfsk8-modem-clean),
which is itself GPL-3.0 (derived from JS8Call-improved, GPL-3.0).

The JS8 protocol was created by Jordan Sherer KN4CRD. The C++ modem implementation
was originally developed by Allan Bazinet W6BAZ and contributors to JS8Call-improved.
JF8Call is a new application built on top of that modem library.
