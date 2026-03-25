#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// IModem implementation wrapping gfsk8-modem-clean (JS8 protocol).

#include "imodem.h"
#include <gfsk8modem.h>

class Gfsk8Modem : public IModem {
public:
    /// Construct a JS8 modem decoder for all submodes.
    explicit Gfsk8Modem(int submodesMask = gfsk8::AllSubmodes);
    ~Gfsk8Modem() override = default;

    // IModem
    std::string       name()         const override { return "JS8"; }
    int               submodeCount() const override { return 5; }
    ModemSubmodeParms submodeParms(int idx) const override;
    int               sampleRate()   const override { return 12000; }

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

    /// Map a sequential UI submode index (0=Normal … 4=Ultra) to the
    /// gfsk8::Submode bitmask value (0,1,2,4,8).
    static gfsk8::Submode toGfsk8Submode(int idx);

private:
    gfsk8::Decoder m_decoder;
};
