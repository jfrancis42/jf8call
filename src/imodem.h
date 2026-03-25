#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Abstract modem interface for JF8Call.
// Allows the JS8 modem (gfsk8-modem-clean) and future modems (Codec2 DATAC,
// etc.) to be used interchangeably without changes to the application layer.

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

// ── Shared types ──────────────────────────────────────────────────────────────

/// A decoded frame returned by IModem::feedAudio().
struct ModemDecoded {
    std::string message;           ///< Physical-layer payload (JS8: 12-char string; Codec2: human-readable text)
    int         snrDb       = 0;
    float       frequencyHz = 0.f;
    float       dtSeconds   = 0.f;
    int         submode     = 0;   ///< Modem-specific submode index (see IModem::submodeParms)
    float       quality     = 0.f;
    int         frameType   = 0;   ///< Modem-specific frame type (JS8 type bits; 0 for others)
    int         utc         = 0;
    int         modemType   = 0;   ///< 0=Gfsk8/JS8, 1=Codec2
    bool        isRawText   = false; ///< true when message is already human-readable (skip JS8 DecodedText)
};

using ModemDecodeCallback = std::function<void(ModemDecoded const &)>;

/// A packed transmit frame ready for IModem::modulate().
struct ModemTxFrame {
    std::string payload;    ///< Encoded payload (modem-specific)
    int         frameType = 0;
    int         submode   = 0;  ///< Submode index (same space as IModem::submodeParms idx)
};

/// Fixed parameters for one modem submode.
struct ModemSubmodeParms {
    std::string name;               ///< Display name, e.g. "Normal", "DATAC0"
    int         periodSeconds = 0;  ///< TX/RX period length; 0 = free-running (Codec2 DATAC)
    bool        utcAligned    = false; ///< true = UTC period boundaries (JS8-style)
    int         startDelayMs  = 0;  ///< Silence before first symbol (TX)
    double      bandwidthHz   = 0.0;///< Approximate occupied bandwidth
};

// ── Abstract modem interface ──────────────────────────────────────────────────

class IModem {
public:
    virtual ~IModem() = default;

    // ── Identity ──────────────────────────────────────────────────────────────

    /// Human-readable modem name, e.g. "JS8" or "Codec2 DATAC".
    virtual std::string       name()         const = 0;

    /// Number of submodes this modem provides.
    virtual int               submodeCount() const = 0;

    /// Fixed parameters for submode index @p idx (0-based, sequential).
    virtual ModemSubmodeParms submodeParms(int idx) const = 0;

    /// Audio sample rate the modem expects/produces (Hz).
    /// AudioInput decimates to this rate before calling feedAudio().
    virtual int               sampleRate()   const = 0;

    // ── TX (stateless) ────────────────────────────────────────────────────────

    /// Pack a human-readable message into one or more transmit frames.
    ///
    /// @param mycall  Sender callsign.
    /// @param mygrid  Sender 4/6-char Maidenhead grid square.
    /// @param text    Human-readable message text.
    /// @param submode Submode index (0-based, see submodeParms()).
    virtual std::vector<ModemTxFrame> pack(
        std::string const &mycall,
        std::string const &mygrid,
        std::string const &text,
        int submode) const = 0;

    /// Modulate a packed frame to float32 PCM at sampleRate() Hz.
    /// The returned vector includes any start-delay silence prefix.
    virtual std::vector<float> modulate(
        ModemTxFrame const &frame,
        double carrierHz = 1500.0) const = 0;

    // ── RX (stateful) ─────────────────────────────────────────────────────────

    /// Feed audio samples to the decoder.
    ///
    /// For period-based modems (JS8): call once per period at the UTC boundary
    /// with a full period's worth of int16 samples at sampleRate() Hz.
    /// @p nutc is the UTC timestamp in JS8 code_time() encoding (HHMM).
    ///
    /// For streaming modems (Codec2 DATAC): call continuously whenever new
    /// samples are available; @p nutc is ignored.
    ///
    /// The callback is invoked synchronously for each decoded frame.
    ///
    /// Not thread-safe: must not be called concurrently.
    virtual void feedAudio(
        std::span<int16_t const> samples,
        int nutc,
        ModemDecodeCallback const &cb) = 0;

    /// Reset internal decoder state (clears soft-combining buffers, sync state, etc.).
    virtual void reset() = 0;
};
