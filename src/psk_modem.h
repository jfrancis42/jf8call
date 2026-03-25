#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// psk_modem.h — IModem implementation wrapping libpsk (PSK31/63/FEC family)

#include "imodem.h"
#include <memory>
#include <mutex>
#include <string>

extern "C" {
#include "psk.h"
}

// ── Submode table ─────────────────────────────────────────────────────────────

struct PskSubmode {
    const char *name;
    psk_mode_t  mode;
    double      bandwidthHz;
};

inline constexpr PskSubmode kPskSubmodes[] = {
    { "BPSK31",   PSK_MODE_BPSK31,    100.0 },
    { "BPSK63",   PSK_MODE_BPSK63,    200.0 },
    { "BPSK125",  PSK_MODE_BPSK125,   400.0 },
    { "PSK63F",   PSK_MODE_PSK63F,    200.0 },
    { "PSK125R",  PSK_MODE_PSK125R,   400.0 },
    { "PSK250R",  PSK_MODE_PSK250R,   800.0 },
    { "PSK500R",  PSK_MODE_PSK500R,  1600.0 },
};
inline constexpr int kPskSubmodeCount =
    static_cast<int>(sizeof(kPskSubmodes) / sizeof(kPskSubmodes[0]));

// modemType constant used in ModemDecoded.modemType
inline constexpr int kPskModemType = 3;

// ── PskModem ──────────────────────────────────────────────────────────────────

class PskModem : public IModem {
public:
    /// @param submodeIdx  Index into kPskSubmodes (0 = BPSK31 default).
    explicit PskModem(int submodeIdx = 0);
    ~PskModem() override;

    // IModem identity
    std::string       name()         const override { return "PSK"; }
    int               submodeCount() const override { return kPskSubmodeCount; }
    ModemSubmodeParms submodeParms(int idx) const override;
    int               sampleRate()   const override { return 8000; }

    // IModem TX (stateless — each call is independent)
    std::vector<ModemTxFrame> pack(
        std::string const &mycall,
        std::string const &mygrid,
        std::string const &text,
        int submode) const override;

    std::vector<float> modulate(
        ModemTxFrame const &frame,
        double carrierHz = 1500.0) const override;

    // IModem RX (stateful — accumulates chars into lines, emits on newline or flush)
    void feedAudio(
        std::span<int16_t const> samples,
        int nutc,
        ModemDecodeCallback const &cb) override;

    void reset() override;

private:
    int        m_submodeIdx;
    psk_rx_t  *m_rx = nullptr;
    mutable std::mutex m_rxMutex;

    // Character accumulation — emitted on newline or kFlushLen chars
    static constexpr int kFlushLen = 256;
    std::string m_rxBuf;
    float       m_lastFreqHz = 1500.0f;
    float       m_lastSnrDb  = 0.0f;

    // Per-call context passed through the C char callback
    struct RxCallCtx {
        PskModem              *self;
        const ModemDecodeCallback *cb;
    };
    static void charCb(char c, void *ud);
    static void statusCb(float snrDb, float freqOffHz, void *ud);

    void emitMessage(const ModemDecodeCallback &cb, bool force = false);
};
