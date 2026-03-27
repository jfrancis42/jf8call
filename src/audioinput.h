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
#include <kissfft/kiss_fft.h>

class AudioInput : public QObject {
    Q_OBJECT
public:
    explicit AudioInput(QObject *parent = nullptr);
    ~AudioInput();

    // Configure decimated output sample rate before calling start().
    // Supported rates: 12000 (default, 4:1 decimation) or 8000 (6:1 decimation).
    void setTargetRate(int hz);

    // Enable streaming chunks: emit audioChunkReady(QByteArray) every chunkMs milliseconds.
    // Call before start(). Set enable=false (default) for period-based modems.
    void setChunkingEnabled(bool enable, int chunkMs = 100);

    // Start audio capture on the named device (empty = default).
    // Returns false on failure.
    bool start(const QString &deviceName);
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Copy the most recent nSamples from the ring buffer at the decimated rate.
    // Returns actual count copied (may be less if buffer not yet full).
    int readLatest(std::vector<float> &out, int nSamples);

    // Copy samples for decoding a full period (period-based modems only).
    // Returns the int16 buffer and the UTC code_time value.
    std::vector<int16_t> takePeriodBuffer(int &utcOut);

    // List available input device names
    static QStringList availableDevices();

signals:
    void error(const QString &msg);
    // Emitted at ~10 Hz with the latest FFT magnitude spectrum (linear power).
    void spectrumReady(std::vector<float> magnitudes, float sampleRateHz);
    // Emitted at ~chunkMs intervals when chunking is enabled (streaming modems).
    // Payload is int16_t samples at the decimated rate.
    void audioChunkReady(QByteArray chunk);

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

    // Ring buffer — sized for 60 s at 12 kHz (large enough for either rate)
    static constexpr int k_ringSize = 720000;
    std::vector<float>   m_ring;
    std::atomic<int>     m_writePos{0};
    int                  m_readPos  = 0;
    mutable std::mutex   m_ringMutex;

    // Configurable decimation
    int                  m_targetRate  = 12000;  ///< Output sample rate (12000 or 8000)
    int                  m_decimation  = 4;      ///< 4:1 for 12kHz, 6:1 for 8kHz

    // FIR decimation filter state
    static constexpr int k_firTaps  = 49;
    std::vector<float>   m_firCoeffs;
    std::vector<float>   m_firBuf;   // overlap-save history
    int                  m_firBufPos = 0;
    int                  m_decimPhase = 0;

    // Streaming chunk output
    bool                 m_chunkingEnabled = false;
    int                  m_chunkTarget     = 0;   ///< samples per chunk
    std::vector<float>   m_chunkBuf;              ///< accumulation buffer

    // Spectrum computation
    int                  m_specTimer = 0;
    std::vector<float>   m_fftBuf;
    static constexpr int k_fftSize  = 2048;

    // Pre-allocated FFT plan and working buffers — created once, reused every call
    kiss_fft_cfg                   m_fftCfg  = nullptr;
    std::vector<kiss_fft_cpx>      m_fftIn;
    std::vector<kiss_fft_cpx>      m_fftOut;
    std::vector<float>             m_fftMag;

    void buildFirFilter();
};
