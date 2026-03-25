#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// olivia_modem.h — IModem implementation wrapping the olivia-modem library

#include "imodem.h"
#include <memory>
#include <string>
#include <mutex>

class OliviaLib;

// ── Submode table ─────────────────────────────────────────────────────────────
// Exposed as a plain array so it can be used by jf8call CMakeLists without
// instantiating the class (same pattern as gfsk8_modem.h kSubmodeMap).

struct OliviaSubmode {
    const char *name;      // display name, e.g. "8/500"
    int         tones;     // 4, 8, 16, 32, 64
    int         bandwidth; // 125, 250, 500, 1000, 2000 Hz
};

inline constexpr OliviaSubmode kOliviaSubmodes[] = {
    // idx 0 — most common HF mode, good first choice
    { "8/500",   8,  500 },
    { "4/250",   4,  250 },
    { "8/250",   8,  250 },
    { "16/500",  16, 500 },
    { "8/1000",  8, 1000 },
    { "16/1000", 16,1000 },
    { "32/1000", 32,1000 },
};
inline constexpr int kOliviaSubmodeCount =
    static_cast<int>(sizeof(kOliviaSubmodes) / sizeof(kOliviaSubmodes[0]));

// ── OliviaModem ───────────────────────────────────────────────────────────────

class OliviaModem : public IModem {
public:
    /// @param submodeIdx  Index into kOliviaSubmodes (0 = 8/500 default).
    explicit OliviaModem(int submodeIdx = 0);
    ~OliviaModem() override;

    // IModem identity
    std::string       name()         const override { return "Olivia"; }
    int               submodeCount() const override { return kOliviaSubmodeCount; }
    ModemSubmodeParms submodeParms(int idx) const override;
    int               sampleRate()   const override { return 8000; }

    // IModem TX
    std::vector<ModemTxFrame> pack(
        std::string const &mycall,
        std::string const &mygrid,
        std::string const &text,
        int submode) const override;

    std::vector<float> modulate(
        ModemTxFrame const &frame,
        double carrierHz = 1500.0) const override;

    // IModem RX
    void feedAudio(
        std::span<int16_t const> samples,
        int nutc,
        ModemDecodeCallback const &cb) override;

    void reset() override;

private:
    int m_submodeIdx;
    mutable std::mutex m_rxMutex;  // protects m_rx across feedAudio / reset

    // We use one OliviaLib per submode index (Rx is stateful across calls).
    // m_rx is the active receiver.
    std::unique_ptr<OliviaLib> m_rx;

    // Character accumulation buffer — emitted as a message on newline or
    // when kFlushLen chars have accumulated.
    static constexpr int kFlushLen = 256;
    std::string m_rxBuf;
    float       m_lastSnr = 0.0f;
    float       m_lastFreqHz = 1500.0f;

    void emitMessage(const ModemDecodeCallback &cb, bool force = false);
    OliviaLib *makeLib(int submodeIdx) const;
};
