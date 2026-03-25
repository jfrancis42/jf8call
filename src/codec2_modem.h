#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// IModem implementation wrapping Codec2 FreeDV DATAC modes.
// Provides three submodes: DATAC0 (~274 bps), DATAC1 (~980 bps), DATAC3 (~2200 bps).
// Operates at 8000 Hz; streaming (burst) detection — no UTC period alignment.

#ifdef HAVE_CODEC2

#include "imodem.h"
#include <vector>
#include <cstdint>

struct freedv;  // opaque FreeDV handle

class Codec2Modem : public IModem {
public:
    /// Construct with a specific DATAC submode index (0=DATAC0, 1=DATAC1, 2=DATAC3).
    explicit Codec2Modem(int submodeIdx = 0);
    ~Codec2Modem() override;

    // IModem
    std::string       name()         const override { return "Codec2"; }
    int               submodeCount() const override { return 3; }
    ModemSubmodeParms submodeParms(int idx) const override;
    int               sampleRate()   const override { return 8000; }

    std::vector<ModemTxFrame> pack(
        std::string const &mycall,
        std::string const &mygrid,
        std::string const &text,
        int submode) const override;

    std::vector<float> modulate(
        ModemTxFrame const &frame,
        double carrierHz = 1500.0) const override;

    void feedAudio(
        std::span<int16_t const> samples,
        int nutc,
        ModemDecodeCallback const &cb) override;

    void reset() override;

    /// Return the FreeDV mode constant for a submode index.
    static int datac_mode_for(int submodeIdx);

private:
    struct freedv *openMode(int submodeIdx) const;

    int          m_submodeIdx;   ///< Configured RX submode
    struct freedv *m_fdvRx = nullptr;  ///< RX context (streaming decoder)

    // TX context — lazily created and cached for the most-recently used submode
    mutable struct freedv *m_fdvTx     = nullptr;
    mutable int            m_txSubmode = -1;

    // RX sample accumulation buffer (feeds freedv_nin() samples at a time)
    std::vector<int16_t> m_rxBuf;
    int m_totalBytesPerFrame = 0; ///< freedv_get_bits_per_modem_frame/8 for RX mode
};

#endif // HAVE_CODEC2
