#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// PortAudio input thread: captures audio, applies 4:1 FIR decimation,
// maintains a 60-second ring buffer at 12 kHz, and feeds the JS8 decoder.

#include <QObject>
#include <QThread>
#include <atomic>
#include <vector>
#include <mutex>
#include <portaudio.h>
#include <gfsk8modem.h>

class AudioInput : public QObject {
    Q_OBJECT
public:
    explicit AudioInput(QObject *parent = nullptr);
    ~AudioInput();

    // Start audio capture on the named device (empty = default).
    // Returns false on failure.
    bool start(const QString &deviceName);
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Copy the most recent nSamples from the ring buffer (at 12 kHz).
    // Returns actual count copied (may be less if buffer not yet full).
    int readLatest(std::vector<float> &out, int nSamples);

    // Copy samples for decoding a full period.
    // Returns the int16 buffer and the UTC code_time value.
    std::vector<int16_t> takePeriodBuffer(gfsk8::Submode submode, int &utcOut);

    // List available input device names
    static QStringList availableDevices();

signals:
    void error(const QString &msg);
    // Emitted at ~10 Hz with the latest FFT magnitude spectrum (linear power).
    void spectrumReady(std::vector<float> magnitudes, float sampleRateHz);

private:
    static int paCallback(const void *input, void *output,
                          unsigned long frameCount,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData);

    void processInputBlock(const float *stereoIn, unsigned long frames);
    void computeSpectrum();

    PaStream         *m_stream     = nullptr;
    std::atomic<bool> m_running{false};

    // Ring buffer at 12 kHz (60 s = 720 000 samples)
    static constexpr int k_ringSize = 720000;
    std::vector<float>   m_ring;
    std::atomic<int>     m_writePos{0};
    int                  m_readPos  = 0;
    mutable std::mutex   m_ringMutex;

    // FIR decimation filter state
    static constexpr int k_firTaps  = 49;
    static constexpr int k_decimation = 4;
    std::vector<float>   m_firCoeffs;
    std::vector<float>   m_firBuf;   // overlap-save history
    int                  m_firBufPos = 0;
    int                  m_decimPhase = 0;

    // Spectrum computation
    int                  m_specTimer = 0;
    std::vector<float>   m_fftBuf;
    static constexpr int k_fftSize  = 2048;

    void buildFirFilter();
};
