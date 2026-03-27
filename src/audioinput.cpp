// SPDX-License-Identifier: GPL-3.0-or-later
#include "audioinput.h"
#include <QDebug>
#include <QDateTime>
#include <cmath>
#include <cstring>
#include <algorithm>

static constexpr float k_pi  = 3.14159265358979323846f;
static constexpr int   k_inputRate  = 48000;  // PortAudio capture rate

// ── FIR filter construction ──────────────────────────────────────────────────
// 49-tap Kaiser-windowed lowpass.
// fc = 0.45 * targetRate / inputRate  (90 % of Nyquist at the decimated rate).
// For 12 kHz output: fc = 5400/48000 = 0.1125 (matches original js8rx/main.cpp).
// For  8 kHz output: fc = 3600/48000 = 0.075.

void AudioInput::buildFirFilter()
{
    const int     N    = k_firTaps;
    const float   fc   = 0.45f * static_cast<float>(m_targetRate) / static_cast<float>(k_inputRate);
    const float   beta = 6.0f;
    const int     M    = N - 1;

    // Compute Kaiser window
    auto bessel_i0 = [](float x) {
        float sum = 1.0f, term = 1.0f;
        for (int k = 1; k <= 30; ++k) {
            term *= (x / 2.0f) / k;
            sum  += term * term;
        }
        return sum;
    };

    const float i0_beta = bessel_i0(beta);
    m_firCoeffs.resize(N);

    for (int n = 0; n < N; ++n) {
        float sinc_val;
        int center = n - M / 2;
        if (center == 0)
            sinc_val = 2.0f * fc;
        else
            sinc_val = std::sin(2.0f * k_pi * fc * center) / (k_pi * center);

        float arg = 1.0f - static_cast<float>(2 * n - M) * static_cast<float>(2 * n - M)
                         / static_cast<float>(M * M);
        arg = std::max(0.0f, arg);
        float window = bessel_i0(beta * std::sqrt(arg)) / i0_beta;
        m_firCoeffs[n] = sinc_val * window;
    }

    // Gain normalisation
    float gain = 0.0f;
    for (float c : m_firCoeffs) gain += c;
    if (gain > 0.0f)
        for (float &c : m_firCoeffs) c /= gain;

    m_firBuf.assign(N, 0.0f);

    // Rebuild FFT plan when target rate changes (k_fftSize is constant, but
    // do it here so the plan is always valid before any audio arrives).
    if (m_fftCfg) kiss_fft_free(m_fftCfg);
    m_fftCfg = kiss_fft_alloc(k_fftSize, 0, nullptr, nullptr);
    m_fftIn .assign(k_fftSize, {0.0f, 0.0f});
    m_fftOut.assign(k_fftSize, {0.0f, 0.0f});
    m_fftMag.assign(k_fftSize / 2, 0.0f);
}

// ── Configuration ────────────────────────────────────────────────────────────

void AudioInput::setTargetRate(int hz)
{
    if (hz != 12000 && hz != 8000) hz = 12000;
    m_targetRate = hz;
    m_decimation = k_inputRate / hz;  // 4 for 12kHz, 6 for 8kHz
    buildFirFilter();
    m_firBufPos  = 0;
    m_decimPhase = 0;
}

void AudioInput::setChunkingEnabled(bool enable, int chunkMs)
{
    m_chunkingEnabled = enable;
    m_chunkTarget     = enable ? (m_targetRate * chunkMs / 1000) : 0;
    m_chunkBuf.clear();
}

// ── AudioInput construction / destruction ─────────────────────────────────

AudioInput::AudioInput(QObject *parent)
    : QObject(parent)
    , m_ring(k_ringSize, 0.0f)
    , m_fftBuf(k_fftSize, 0.0f)
{
    buildFirFilter();
    Pa_Initialize();
}

AudioInput::~AudioInput()
{
    stop();
    Pa_Terminate();
    if (m_fftCfg) kiss_fft_free(m_fftCfg);
}

// ── PortAudio callback ────────────────────────────────────────────────────────

int AudioInput::paCallback(const void *input, void * /*output*/,
                           unsigned long frameCount,
                           const PaStreamCallbackTimeInfo * /*ti*/,
                           PaStreamCallbackFlags /*flags*/,
                           void *userData)
{
    auto *self = static_cast<AudioInput *>(userData);
    const float *in = static_cast<const float *>(input);
    if (in)
        self->processInputBlock(in, frameCount);
    return paContinue;
}

void AudioInput::processInputBlock(const float *stereoIn, unsigned long frames)
{
    // Extract left channel, apply FIR + N:1 decimation → m_targetRate
    for (unsigned long i = 0; i < frames; ++i) {
        float sample = stereoIn[i * 2];  // left channel of stereo

        // Shift FIR delay line
        m_firBuf[m_firBufPos] = sample;
        m_firBufPos = (m_firBufPos + 1) % k_firTaps;

        ++m_decimPhase;
        if (m_decimPhase < m_decimation) continue;
        m_decimPhase = 0;

        // Compute FIR output
        float out = 0.0f;
        int pos = m_firBufPos;
        for (int k = 0; k < k_firTaps; ++k) {
            pos = (pos - 1 + k_firTaps) % k_firTaps;
            out += m_firCoeffs[k] * m_firBuf[pos];
        }

        // Write to ring buffer
        int wp = m_writePos.load(std::memory_order_relaxed);
        m_ring[wp % k_ringSize] = out;
        m_writePos.store(wp + 1, std::memory_order_release);

        // Accumulate for spectrum
        m_fftBuf[m_specTimer % k_fftSize] = out;
        ++m_specTimer;
        if (m_specTimer == k_fftSize) {
            computeSpectrum();
            m_specTimer = 0;
        }

        // Streaming chunk output (for Codec2 and other streaming modems)
        if (m_chunkingEnabled && m_chunkTarget > 0) {
            m_chunkBuf.push_back(out);
            if (static_cast<int>(m_chunkBuf.size()) >= m_chunkTarget) {
                // Convert float to int16 and emit
                QByteArray ba;
                ba.resize(static_cast<int>(m_chunkBuf.size() * sizeof(int16_t)));
                int16_t *dst = reinterpret_cast<int16_t *>(ba.data());
                for (size_t n = 0; n < m_chunkBuf.size(); ++n)
                    dst[n] = static_cast<int16_t>(
                        std::clamp(m_chunkBuf[n] * 32767.0f, -32767.0f, 32767.0f));
                emit audioChunkReady(ba);
                m_chunkBuf.clear();
            }
        }
    }
}

void AudioInput::computeSpectrum()
{
    if (!m_fftCfg) return;

    // Apply Hann window into pre-allocated input buffer
    for (int i = 0; i < k_fftSize; ++i) {
        float w = 0.5f * (1.0f - std::cos(2.0f * k_pi * i / (k_fftSize - 1)));
        m_fftIn[i].r = m_fftBuf[i] * w;
        m_fftIn[i].i = 0.0f;
    }

    kiss_fft(m_fftCfg, m_fftIn.data(), m_fftOut.data());

    for (int i = 0; i < k_fftSize / 2; ++i) {
        float re = m_fftOut[i].r, im = m_fftOut[i].i;
        m_fftMag[i] = std::sqrt(re * re + im * im);
    }

    emit spectrumReady(m_fftMag, static_cast<float>(m_targetRate));
}

// ── start / stop ─────────────────────────────────────────────────────────────

bool AudioInput::start(const QString &deviceName)
{
    if (m_running.load()) stop();

    // Find device index
    int deviceIndex = Pa_GetDefaultInputDevice();
    if (!deviceName.isEmpty()) {
        int count = Pa_GetDeviceCount();
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
            if (info && info->maxInputChannels > 0) {
                if (QString::fromUtf8(info->name).contains(deviceName, Qt::CaseInsensitive)) {
                    deviceIndex = i;
                    break;
                }
            }
        }
    }

    if (deviceIndex == paNoDevice) {
        emit error(QStringLiteral("No audio input device found"));
        return false;
    }

    PaStreamParameters params{};
    params.device                    = deviceIndex;
    params.channelCount              = 2;  // stereo (PCM2901 is stereo)
    params.sampleFormat              = paFloat32;
    params.suggestedLatency          = Pa_GetDeviceInfo(deviceIndex)->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(
        &m_stream,
        &params,
        nullptr,         // no output
        k_inputRate,     // 48 kHz
        1024,            // frames per callback
        paClipOff,
        &AudioInput::paCallback,
        this);

    if (err != paNoError) {
        emit error(QStringLiteral("Failed to open audio input: %1")
                   .arg(QString::fromLatin1(Pa_GetErrorText(err))));
        m_stream = nullptr;
        return false;
    }

    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        emit error(QStringLiteral("Failed to start audio input: %1")
                   .arg(QString::fromLatin1(Pa_GetErrorText(err))));
        return false;
    }

    m_running.store(true);
    return true;
}

void AudioInput::stop()
{
    if (m_stream) {
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }
    m_running.store(false);
}

// ── Buffer accessors ──────────────────────────────────────────────────────────

int AudioInput::readLatest(std::vector<float> &out, int nSamples)
{
    int wp = m_writePos.load(std::memory_order_acquire);
    int available = std::min(wp, k_ringSize);
    int n = std::min(nSamples, available);
    if (n <= 0) return 0;

    out.resize(n);
    int start = (wp - n + k_ringSize) % k_ringSize;
    for (int i = 0; i < n; ++i)
        out[i] = m_ring[(start + i) % k_ringSize];
    return n;
}

std::vector<int16_t> AudioInput::takePeriodBuffer(int &utcOut)
{
    const QDateTime u = QDateTime::currentDateTimeUtc();
    // Encode as seconds-since-midnight so the decoder can compute
    // UTC-aligned kpos from the actual buffer capture time rather
    // than the (potentially delayed) decode time.
    utcOut = u.time().hour() * 3600 + u.time().minute() * 60 + u.time().second();
    // Use the full 60-second ring buffer for best decoder performance
    constexpr int bufSamples = k_ringSize;

    int wp = m_writePos.load(std::memory_order_acquire);
    int available = std::min(wp, k_ringSize);
    int n = std::min(bufSamples, available);

    std::vector<float> floatBuf(n);
    int start = (wp - n + k_ringSize) % k_ringSize;
    for (int i = 0; i < n; ++i)
        floatBuf[i] = m_ring[(start + i) % k_ringSize];

    // Convert float to int16 (scale by 32767)
    std::vector<int16_t> result(n);
    for (int i = 0; i < n; ++i)
        result[i] = static_cast<int16_t>(std::clamp(floatBuf[i] * 32767.0f, -32767.0f, 32767.0f));

    return result;
}

QStringList AudioInput::availableDevices()
{
    QStringList names;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0)
            names.append(QString::fromUtf8(info->name));
    }
    return names;
}
