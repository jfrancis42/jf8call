# Voice + Data Mode (FreeDV / Codec2)

## Idea

Add a FreeDV voice channel to JF8Call alongside the existing JS8 text channel,
giving operators a single application for both weak-signal text messaging (JS8)
and digital voice (FreeDV) on HF.

## Why it's a natural fit

- Both modes share the same audio I/O (PortAudio), radio control (Hamlib), and
  waterfall display that JF8Call already has.
- `libcodec2` provides both the voice codec (FreeDV 700D/2020) and a standalone
  OFDM data modem with LDPC — one library, two features.
- FreeDV 700D operates around −2 dB SNR; FreeDV 2020 around +2 dB. Both are
  practical on HF at normal power levels.
- The Codec2 OFDM data mode (same library) can also serve as a *fast data mode*
  at 500–2400 bps — useful for file transfer or rapid keyboard exchange when
  conditions are good.

## Implementation sketch

- Add `libcodec2` as an optional dependency (LGPL-2.1, compatible with GPL-3.0).
- New `FreeDVController` class (similar to `HamlibController`) wrapping the
  `freedv_open()` / `freedv_tx()` / `freedv_rx()` API.
- PTT and period management already exist; voice TX just needs a continuous
  audio stream rather than period-synchronized bursts.
- UI: add a mode selector (JS8 / FreeDV voice / Codec2 data) to the toolbar.
- WebSocket API: expose `freedv_tx_start`, `freedv_tx_stop`, `freedv_rx_text`
  events to maintain API parity.

## References

- libcodec2: https://github.com/drowe67/codec2
- FreeDV API: codec2/src/freedv_api.h
- David Rowe VK5DGR blog: rowetel.com
