// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef HAVE_CODEC2

#include "codec2_modem.h"
#include <codec2/freedv_api.h>
#include <algorithm>
#include <cstring>
#include <string>

// ── Submode tables ─────────────────────────────────────────────────────────────

static const int kDatacModes[3] = {
    FREEDV_MODE_DATAC0,   // 0 — ~274 bps,  ~500 Hz BW, very robust
    FREEDV_MODE_DATAC1,   // 1 — ~980 bps, ~1600 Hz BW
    FREEDV_MODE_DATAC3,   // 2 — ~2200 bps, ~2400 Hz BW
};

static const char *kDatacNames[3] = {
    "DATAC0 — 500 Hz / 274 bps",    // narrowest, most robust
    "DATAC1 — 1.6 kHz / 980 bps",   // mid-range
    "DATAC3 — 2.4 kHz / 2.2 kbps",  // highest throughput
};

// Approximate bandwidths (Hz) for display
static const double kDatacBw[3] = { 500.0, 1600.0, 2400.0 };

// ── Text-frame framing ────────────────────────────────────────────────────────
// Byte 0: flags (0x01 = UTF-8 text frame)
// Bytes 1..(totalBytes-3): null-terminated UTF-8 text, zero-padded
// Bytes (totalBytes-2)..(totalBytes-1): CRC-16/KERMIT computed over bytes 0..(totalBytes-3)

static constexpr uint8_t kFlagText = 0x01;
static constexpr uint8_t kFlagSync = 0x02; ///< sync heartbeat frame (empty payload)
static constexpr uint8_t kFlagEom  = 0x04; ///< end-of-message frame (empty payload)

// ── Helpers ───────────────────────────────────────────────────────────────────

int Codec2Modem::datac_mode_for(int idx)
{
    if (idx >= 0 && idx < 3) return kDatacModes[idx];
    return FREEDV_MODE_DATAC0;
}

struct freedv *Codec2Modem::openMode(int idx) const
{
    struct freedv *fdv = freedv_open(datac_mode_for(idx));
    if (fdv)
        freedv_set_frames_per_burst(fdv, 1);
    return fdv;
}

static ModemTxFrame makeEmptyFrame(int totalBytes, uint8_t flag, int submode)
{
    std::vector<uint8_t> bytes(static_cast<size_t>(totalBytes), 0u);
    bytes[0] = flag;
    // CRC over payload region (everything except last 2 bytes)
    int payloadBytes = totalBytes - 2;
    uint16_t crc = freedv_gen_crc16(bytes.data(), payloadBytes);
    bytes[static_cast<size_t>(payloadBytes)]     = static_cast<uint8_t>(crc >> 8);
    bytes[static_cast<size_t>(payloadBytes) + 1] = static_cast<uint8_t>(crc & 0xff);
    ModemTxFrame f;
    f.payload   = std::string(reinterpret_cast<const char *>(bytes.data()),
                               static_cast<size_t>(totalBytes));
    f.frameType = 0;
    f.submode   = submode;
    return f;
}

// ── Construction / destruction ────────────────────────────────────────────────

Codec2Modem::Codec2Modem(int submodeIdx)
    : m_submodeIdx(std::clamp(submodeIdx, 0, 2))
{
    m_fdvRx = openMode(m_submodeIdx);
    if (m_fdvRx)
        m_totalBytesPerFrame = freedv_get_bits_per_modem_frame(m_fdvRx) / 8;
}

Codec2Modem::~Codec2Modem()
{
    if (m_fdvRx) { freedv_close(m_fdvRx); m_fdvRx = nullptr; }
    if (m_fdvTx) { freedv_close(m_fdvTx); m_fdvTx = nullptr; }
}

// ── IModem ────────────────────────────────────────────────────────────────────

ModemSubmodeParms Codec2Modem::submodeParms(int idx) const
{
    ModemSubmodeParms p;
    if (idx < 0 || idx >= 3) idx = 0;
    p.name          = kDatacNames[idx];
    p.periodSeconds = 0;        // streaming — no UTC period alignment
    p.utcAligned    = false;
    p.startDelayMs  = 0;
    p.bandwidthHz   = kDatacBw[idx];
    return p;
}

std::vector<ModemTxFrame> Codec2Modem::pack(
    std::string const &mycall,
    std::string const & /*mygrid*/,
    std::string const &text,
    int submode) const
{
    submode = std::clamp(submode, 0, 2);

    // Ensure TX context exists for this submode
    if (!m_fdvTx || m_txSubmode != submode) {
        if (m_fdvTx) freedv_close(m_fdvTx);
        m_fdvTx = openMode(submode);
        m_txSubmode = submode;
    }
    if (!m_fdvTx) return {};

    int totalBytes   = freedv_get_bits_per_modem_frame(m_fdvTx) / 8;
    int payloadBytes = totalBytes - 2;   // last 2 bytes reserved for CRC

    // Build full message string: "CALLSIGN: text"
    std::string fullMsg;
    if (!mycall.empty())
        fullMsg = mycall + ": " + text;
    else
        fullMsg = text;

    // Each frame carries: flags(1) + up to (payloadBytes-1) text bytes
    int textPerFrame = payloadBytes - 1;
    if (textPerFrame <= 0) return {};

    std::vector<ModemTxFrame> out;
    size_t offset = 0;
    int dataFrameCount = 0;

    do {
        std::string chunk = fullMsg.substr(offset, static_cast<size_t>(textPerFrame));
        offset += chunk.size();

        std::vector<uint8_t> bytesIn(static_cast<size_t>(totalBytes), 0u);
        bytesIn[0] = kFlagText;
        std::memcpy(&bytesIn[1], chunk.data(), chunk.size());

        uint16_t crc = freedv_gen_crc16(bytesIn.data(), payloadBytes);
        bytesIn[static_cast<size_t>(payloadBytes)]     = static_cast<uint8_t>(crc >> 8);
        bytesIn[static_cast<size_t>(payloadBytes) + 1] = static_cast<uint8_t>(crc & 0xff);

        ModemTxFrame f;
        f.payload  = std::string(reinterpret_cast<const char *>(bytesIn.data()),
                                 static_cast<size_t>(totalBytes));
        f.frameType = 0;
        f.submode   = submode;
        out.push_back(std::move(f));
        ++dataFrameCount;

        // Inject a sync frame every kCodec2SyncInterval data frames.
        if (dataFrameCount % kCodec2SyncInterval == 0)
            out.push_back(makeEmptyFrame(totalBytes, kFlagSync, submode));

    } while (offset < fullMsg.size());

    // Append EOM frame after all data (and any trailing sync) frames.
    out.push_back(makeEmptyFrame(totalBytes, kFlagEom, submode));

    return out;
}

std::vector<float> Codec2Modem::modulate(
    ModemTxFrame const &frame,
    double /* carrierHz */) const
{
    int submode = std::clamp(frame.submode, 0, 2);

    // Ensure TX context for this submode
    if (!m_fdvTx || m_txSubmode != submode) {
        if (m_fdvTx) freedv_close(m_fdvTx);
        m_fdvTx = openMode(submode);
        m_txSubmode = submode;
    }
    if (!m_fdvTx) return {};

    int totalBytes  = freedv_get_bits_per_modem_frame(m_fdvTx) / 8;
    int nTxSamples  = freedv_get_n_tx_modem_samples(m_fdvTx);

    // Build the byte buffer from frame payload
    std::vector<uint8_t> bytesIn(static_cast<size_t>(totalBytes), 0u);
    size_t copyLen = std::min(static_cast<size_t>(totalBytes), frame.payload.size());
    std::memcpy(bytesIn.data(), frame.payload.data(), copyLen);

    // Scratch buffer for modulated samples
    std::vector<int16_t> scratch(static_cast<size_t>(nTxSamples));
    std::vector<int16_t> allSamples;

    // Preamble
    int nPre = freedv_rawdatapreambletx(m_fdvTx, scratch.data());
    allSamples.insert(allSamples.end(), scratch.begin(), scratch.begin() + nPre);

    // Data frame
    freedv_rawdatatx(m_fdvTx, scratch.data(), bytesIn.data());
    allSamples.insert(allSamples.end(), scratch.begin(), scratch.begin() + nTxSamples);

    // Postamble
    int nPost = freedv_rawdatapostambletx(m_fdvTx, scratch.data());
    allSamples.insert(allSamples.end(), scratch.begin(), scratch.begin() + nPost);

    // ~200 ms silence between bursts at 8000 Hz = 1600 samples
    allSamples.insert(allSamples.end(), 1600, 0);

    // Convert int16 → float32
    std::vector<float> result;
    result.reserve(allSamples.size());
    for (int16_t s : allSamples)
        result.push_back(static_cast<float>(s) / 32768.0f);

    return result;
}

void Codec2Modem::feedAudio(
    std::span<int16_t const> samples,
    int /* nutc */,
    ModemDecodeCallback const &cb)
{
    if (!m_fdvRx) return;

    m_rxBuf.insert(m_rxBuf.end(), samples.begin(), samples.end());

    std::vector<uint8_t> bytesOut(static_cast<size_t>(m_totalBytesPerFrame));

    while (true) {
        int nin = freedv_nin(m_fdvRx);
        if (static_cast<int>(m_rxBuf.size()) < nin) break;

        int nbytes = freedv_rawdatarx(m_fdvRx, bytesOut.data(), m_rxBuf.data());
        m_rxBuf.erase(m_rxBuf.begin(), m_rxBuf.begin() + nin);

        if (nbytes <= 0) continue;

        // nbytes == m_totalBytesPerFrame when a frame is received.
        // Payload is bytes[0..totalBytes-3]; bytes[totalBytes-2..totalBytes-1] = CRC.
        int payloadBytes = m_totalBytesPerFrame - 2;
        if (payloadBytes < 2) continue;

        // Verify CRC
        uint16_t rxCrc   = (static_cast<uint16_t>(bytesOut[static_cast<size_t>(payloadBytes)]) << 8)
                         |  static_cast<uint16_t>(bytesOut[static_cast<size_t>(payloadBytes) + 1]);
        uint16_t calcCrc = freedv_gen_crc16(bytesOut.data(), payloadBytes);
        if (rxCrc != calcCrc) continue;

        // Dispatch on frame type flag.
        const uint8_t flag = bytesOut[0];

        if (flag == kFlagSync) {
            // Sync heartbeat frame: notify receiver of transmission pace.
            ModemDecoded syncOut;
            syncOut.isSyncMark = true;
            syncOut.modemType  = 1;
            syncOut.submode    = m_submodeIdx;
            syncOut.isRawText  = true;
            cb(syncOut);
            continue;
        }

        if (flag == kFlagEom) {
            // End-of-message frame.
            ModemDecoded eomOut;
            eomOut.isEom      = true;
            eomOut.modemType  = 1;
            eomOut.submode    = m_submodeIdx;
            eomOut.isRawText  = true;
            cb(eomOut);
            continue;
        }

        if (flag != kFlagText) continue;

        // Extract null-terminated text from bytes[1..payloadBytes-1]
        const char *textPtr = reinterpret_cast<const char *>(&bytesOut[1]);
        int maxLen = payloadBytes - 1;
        int textLen = 0;
        while (textLen < maxLen && textPtr[textLen] != '\0') ++textLen;
        if (textLen == 0) continue;

        ModemDecoded out;
        out.message     = std::string(textPtr, static_cast<size_t>(textLen));
        out.isRawText   = true;
        out.modemType   = 1;
        out.submode     = m_submodeIdx;
        out.snrDb       = 0;
        out.frequencyHz = 0.0f;
        out.dtSeconds   = 0.0f;
        out.frameType   = 0;
        cb(out);
    }
}

void Codec2Modem::reset()
{
    m_rxBuf.clear();
    // Re-open the RX context to reset FreeDV internal sync state
    if (m_fdvRx) { freedv_close(m_fdvRx); m_fdvRx = nullptr; }
    m_fdvRx = openMode(m_submodeIdx);
    if (m_fdvRx)
        m_totalBytesPerFrame = freedv_get_bits_per_modem_frame(m_fdvRx) / 8;
}

#endif // HAVE_CODEC2
