// SPDX-License-Identifier: GPL-3.0-or-later
#include "olivia_modem.h"
#include "oliviamodem.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

// ── Helpers ───────────────────────────────────────────────────────────────────

OliviaLib *OliviaModem::makeLib(int idx) const
{
    const int clamped = std::clamp(idx, 0, kOliviaSubmodeCount - 1);
    const auto &sm = kOliviaSubmodes[clamped];
    OliviaConfig cfg;
    cfg.tones       = sm.tones;
    cfg.bandwidthHz = sm.bandwidth;
    cfg.syncMargin  = 8;
    cfg.syncIntegLen= 4;
    cfg.squelch     = 0.0;
    cfg.sendTones   = true;
    return new OliviaLib(cfg);
}

// ── Construction ──────────────────────────────────────────────────────────────

OliviaModem::OliviaModem(int submodeIdx)
    : m_submodeIdx(std::clamp(submodeIdx, 0, kOliviaSubmodeCount - 1))
    , m_rx(makeLib(m_submodeIdx))
{}

OliviaModem::~OliviaModem() = default;

// ── IModem identity ───────────────────────────────────────────────────────────

ModemSubmodeParms OliviaModem::submodeParms(int idx) const
{
    const int clamped = std::clamp(idx, 0, kOliviaSubmodeCount - 1);
    const auto &sm = kOliviaSubmodes[clamped];
    ModemSubmodeParms p;
    p.name          = std::string("Olivia ") + sm.name;
    p.periodSeconds = 0;      // streaming — no UTC period boundaries
    p.utcAligned    = false;
    p.startDelayMs  = 0;
    p.bandwidthHz   = static_cast<double>(sm.bandwidth);
    return p;
}

// ── IModem TX ─────────────────────────────────────────────────────────────────

std::vector<ModemTxFrame> OliviaModem::pack(
    std::string const &mycall,
    std::string const & /*mygrid*/,
    std::string const &text,
    int submode) const
{
    const int sm = std::clamp(submode, 0, kOliviaSubmodeCount - 1);
    const std::string fullText = mycall + ": " + text;

    // Inject sync heartbeats and an EOM marker (same protocol as PSK modem).
    std::string payload;
    payload.reserve(fullText.size() + fullText.size() / kStreamSyncInterval + 2);
    for (size_t i = 0; i < fullText.size(); ++i) {
        if (i > 0 && (i % static_cast<size_t>(kStreamSyncInterval)) == 0)
            payload.push_back(kStreamSyncChar);
        payload.push_back(fullText[i]);
    }
    payload.push_back(kStreamEomChar);

    ModemTxFrame f;
    f.payload   = std::move(payload);
    f.frameType = 0;
    f.submode   = sm;
    return { f };
}

std::vector<float> OliviaModem::modulate(
    ModemTxFrame const &frame,
    double carrierHz) const
{
    const int sm = std::clamp(frame.submode, 0, kOliviaSubmodeCount - 1);

    // Build a temporary OliviaLib for TX (TX is stateless — each call is
    // independent and produces a complete waveform).
    std::unique_ptr<OliviaLib> tx(makeLib(sm));
    std::vector<double> pcmDouble = tx->modulate(frame.payload, carrierHz);

    // Convert double → float (IModem contract: float PCM at sampleRate() Hz)
    std::vector<float> out(pcmDouble.size());
    for (size_t i = 0; i < pcmDouble.size(); ++i)
        out[i] = static_cast<float>(std::clamp(pcmDouble[i], -1.0, 1.0));
    return out;
}

// ── IModem RX ─────────────────────────────────────────────────────────────────

void OliviaModem::feedAudio(
    std::span<int16_t const> samples,
    int /*nutc*/,
    ModemDecodeCallback const &cb)
{
    if (samples.empty()) return;

    // Convert int16 → double [-1..+1]
    std::vector<double> buf(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
        buf[i] = samples[i] / 32767.0;

    std::lock_guard<std::mutex> lock(m_rxMutex);

    m_lastSnr    = static_cast<float>(m_rx->snr());
    m_lastFreqHz = static_cast<float>(1500.0 + m_rx->freqOffsetHz());

    m_rx->feedAudio(buf.data(), static_cast<int>(buf.size()),
        [this, &cb](uint8_t ch) {
            const char c = static_cast<char>(ch);
            if (c == kStreamSyncChar) {
                // Sync heartbeat: emit sync event, don't accumulate.
                ModemDecoded d;
                d.isSyncMark  = true;
                d.frequencyHz = m_lastFreqHz;
                d.snrDb       = static_cast<int>(10.0f * std::log10(
                                    std::max(m_lastSnr, 1e-6f)));
                d.modemType   = 2;
                d.submode     = m_submodeIdx;
                d.isRawText   = true;
                cb(d);
                return;
            }
            if (c == kStreamEomChar) {
                emitMessage(cb, /*eom=*/true);
                return;
            }
            m_rxBuf.push_back(c);
            if (ch == '\n' || static_cast<int>(m_rxBuf.size()) >= kFlushLen)
                emitMessage(cb, /*eom=*/false);
        });
}

void OliviaModem::emitMessage(const ModemDecodeCallback &cb, bool eom)
{
    // Trim trailing whitespace/nulls that Olivia commonly pads
    while (!m_rxBuf.empty() &&
           (m_rxBuf.back() == '\0' || m_rxBuf.back() == ' ' ||
            m_rxBuf.back() == '\r'))
        m_rxBuf.pop_back();

    if (m_rxBuf.empty()) return;

    ModemDecoded d;
    d.message     = m_rxBuf;
    d.snrDb       = static_cast<int>(10.0f * std::log10(
                        std::max(m_lastSnr, 1e-6f)));
    d.frequencyHz = m_lastFreqHz;
    d.submode     = m_submodeIdx;
    d.modemType   = 2;
    d.isRawText   = true;
    d.frameType   = 0;
    d.isEom       = eom;

    cb(d);
    m_rxBuf.clear();
}

void OliviaModem::reset()
{
    std::lock_guard<std::mutex> lock(m_rxMutex);
    m_rx->reset();
    m_rxBuf.clear();
}
