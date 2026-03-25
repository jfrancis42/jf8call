// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfsk8_modem.h"

// gfsk8::Submode uses bitmask values (Normal=0, Fast=1, Turbo=2, Slow=4, Ultra=8).
// JF8Call uses sequential UI indices (0=Normal, 1=Fast, 2=Turbo, 3=Slow, 4=Ultra).
// This table maps UI index → gfsk8::Submode.
static const gfsk8::Submode kSubmodeMap[5] = {
    gfsk8::Submode::Normal,   // 0
    gfsk8::Submode::Fast,     // 1
    gfsk8::Submode::Turbo,    // 2
    gfsk8::Submode::Slow,     // 3
    gfsk8::Submode::Ultra,    // 4
};

// ── Construction ──────────────────────────────────────────────────────────────

Gfsk8Modem::Gfsk8Modem(int submodesMask)
    : m_decoder(submodesMask)
{}

// ── Static helper ─────────────────────────────────────────────────────────────

gfsk8::Submode Gfsk8Modem::toGfsk8Submode(int idx)
{
    if (idx >= 0 && idx < 5)
        return kSubmodeMap[idx];
    return gfsk8::Submode::Normal;
}

// ── IModem ────────────────────────────────────────────────────────────────────

ModemSubmodeParms Gfsk8Modem::submodeParms(int idx) const
{
    const auto &p = gfsk8::submodeParms(toGfsk8Submode(idx));
    ModemSubmodeParms out;
    static const char *names[5] = {"Normal", "Fast", "Turbo", "Slow", "Ultra"};
    out.name          = (idx >= 0 && idx < 5) ? names[idx] : "?";
    out.periodSeconds = p.periodSeconds;
    out.utcAligned    = true;
    out.startDelayMs  = p.startDelayMs;
    out.bandwidthHz   = p.toneSpacingHz * 8.0; // 8-FSK: 8 tones
    return out;
}

std::vector<ModemTxFrame> Gfsk8Modem::pack(
    std::string const &mycall,
    std::string const &mygrid,
    std::string const &text,
    int submode) const
{
    auto gfskFrames = gfsk8::pack(mycall, mygrid, text, toGfsk8Submode(submode));
    std::vector<ModemTxFrame> out;
    out.reserve(gfskFrames.size());
    for (auto const &f : gfskFrames)
        out.push_back({f.payload, f.frameType, submode});
    return out;
}

std::vector<float> Gfsk8Modem::modulate(
    ModemTxFrame const &frame,
    double carrierHz) const
{
    return gfsk8::modulate(toGfsk8Submode(frame.submode),
                           frame.frameType,
                           frame.payload,
                           carrierHz);
}

void Gfsk8Modem::feedAudio(
    std::span<int16_t const> samples,
    int nutc,
    ModemDecodeCallback const &cb)
{
    m_decoder.decode(samples, nutc, [&](gfsk8::Decoded const &d) {
        ModemDecoded out;
        out.message     = d.message;
        out.snrDb       = d.snrDb;
        out.frequencyHz = d.frequencyHz;
        out.dtSeconds   = d.dtSeconds;
        out.submode     = d.submode;
        out.quality     = d.quality;
        out.frameType   = d.frameType;
        out.utc         = d.utc;
        cb(out);
    });
}

void Gfsk8Modem::reset()
{
    m_decoder.reset();
}
