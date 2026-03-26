// SPDX-License-Identifier: GPL-3.0-or-later
#include "psk_modem.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

// ── Construction ──────────────────────────────────────────────────────────────

PskModem::PskModem(int submodeIdx)
    : m_submodeIdx(std::clamp(submodeIdx, 0, kPskSubmodeCount - 1))
{
    const psk_mode_t mode = kPskSubmodes[m_submodeIdx].mode;
    m_rx = psk_rx_create(8000u, 1500.0f, mode);
    if (!m_rx)
        throw std::runtime_error("PskModem: psk_rx_create failed");

    psk_rx_set_status_callback(m_rx, statusCb, this);
    // char callback is set per-call in feedAudio so we have access to the cb
}

PskModem::~PskModem()
{
    psk_rx_destroy(m_rx);
}

// ── IModem identity ───────────────────────────────────────────────────────────

ModemSubmodeParms PskModem::submodeParms(int idx) const
{
    const int i = std::clamp(idx, 0, kPskSubmodeCount - 1);
    const auto &sm = kPskSubmodes[i];
    ModemSubmodeParms p;
    p.name          = std::string("PSK ") + sm.name;
    p.periodSeconds = 0;       // streaming — no UTC period boundaries
    p.utcAligned    = false;
    p.startDelayMs  = 0;
    p.bandwidthHz   = sm.bandwidthHz;
    return p;
}

// ── IModem TX ─────────────────────────────────────────────────────────────────

std::vector<ModemTxFrame> PskModem::pack(
    std::string const &mycall,
    std::string const & /*mygrid*/,
    std::string const &text,
    int submode) const
{
    const int sm = std::clamp(submode, 0, kPskSubmodeCount - 1);
    const std::string fullText = mycall + ": " + text;

    // Inject sync heartbeats every kStreamSyncInterval characters and
    // append an EOM marker at the end so the receiver can detect message
    // boundaries and transmission pace.
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

std::vector<float> PskModem::modulate(
    ModemTxFrame const &frame,
    double carrierHz) const
{
    const int sm = std::clamp(frame.submode, 0, kPskSubmodeCount - 1);
    const psk_mode_t mode = kPskSubmodes[sm].mode;

    psk_tx_t *tx = psk_tx_create(8000u, static_cast<float>(carrierHz), mode);
    if (!tx) return {};

    psk_tx_write(tx, frame.payload.c_str(), frame.payload.size());

    // Allocate a generous buffer: 10 s at 8000 Hz is more than enough for any
    // PSK frame including the FEC pipeline flush.
    constexpr size_t kMaxFrames = 80000;
    std::vector<float> out(kMaxFrames);
    const size_t n = psk_tx_read(tx, out.data(), kMaxFrames);
    out.resize(n);

    psk_tx_destroy(tx);
    return out;
}

// ── IModem RX ─────────────────────────────────────────────────────────────────

void PskModem::charCb(char c, void *ud)
{
    auto *ctx = static_cast<RxCallCtx *>(ud);
    PskModem *self = ctx->self;

    if (c == kStreamSyncChar) {
        // Sync heartbeat: emit a sync event (no display text) so the receiver
        // can track transmission pace and detect gaps.
        ModemDecoded d;
        d.isSyncMark  = true;
        d.frequencyHz = self->m_lastFreqHz;
        d.snrDb       = static_cast<int>(self->m_lastSnrDb);
        d.modemType   = kPskModemType;
        d.submode     = self->m_submodeIdx;
        d.isRawText   = true;
        (*ctx->cb)(d);
        return;
    }

    if (c == kStreamEomChar) {
        // End-of-message: flush accumulated text with EOM flag.
        self->emitMessage(*ctx->cb, /*eom=*/true);
        return;
    }

    self->m_rxBuf.push_back(c);
    if (c == '\n' || static_cast<int>(self->m_rxBuf.size()) >= kFlushLen)
        self->emitMessage(*ctx->cb, /*eom=*/false);
}

void PskModem::statusCb(float snrDb, float freqOffHz, void *ud)
{
    auto *self = static_cast<PskModem *>(ud);
    self->m_lastSnrDb  = snrDb;
    self->m_lastFreqHz = 1500.0f + freqOffHz;
}

void PskModem::feedAudio(
    std::span<int16_t const> samples,
    int /*nutc*/,
    ModemDecodeCallback const &cb)
{
    if (samples.empty()) return;

    // Convert int16 → float32 [-1..+1]
    std::vector<float> buf(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
        buf[i] = samples[i] / 32767.0f;

    std::lock_guard<std::mutex> lock(m_rxMutex);

    RxCallCtx ctx{ this, &cb };
    psk_rx_set_char_callback(m_rx, charCb, &ctx);
    psk_rx_feed(m_rx, buf.data(), buf.size());
}

void PskModem::emitMessage(const ModemDecodeCallback &cb, bool eom)
{
    // Strip trailing nulls / carriage returns common in PSK idle
    while (!m_rxBuf.empty() &&
           (m_rxBuf.back() == '\0' || m_rxBuf.back() == '\r'))
        m_rxBuf.pop_back();

    if (m_rxBuf.empty()) return;

    ModemDecoded d;
    d.message     = m_rxBuf;
    d.snrDb       = static_cast<int>(m_lastSnrDb);
    d.frequencyHz = m_lastFreqHz;
    d.submode     = m_submodeIdx;
    d.modemType   = kPskModemType;
    d.isRawText   = true;
    d.frameType   = 0;
    d.isEom       = eom;

    cb(d);
    m_rxBuf.clear();
}

void PskModem::reset()
{
    std::lock_guard<std::mutex> lock(m_rxMutex);
    psk_rx_reset(m_rx);
    m_rxBuf.clear();
}
