#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// PortAudio output: plays modulated JS8 audio with PTT control.

#include <QObject>
#include <atomic>
#include <vector>
#include <mutex>
#include <portaudio.h>

class AudioOutput : public QObject {
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();

    bool open(const QString &deviceName);
    void close();

    // Queue a buffer of 48 kHz int16 samples for playback.
    // Replaces any currently queued buffer.
    void play(std::vector<int16_t> samples);

    // True while audio is being played back.
    bool isPlaying() const { return m_playing.load(); }

    static QStringList availableDevices();

signals:
    void error(const QString &msg);
    void playbackStarted();
    void playbackFinished();

private:
    static int paCallback(const void *input, void *output,
                          unsigned long frameCount,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData);

    PaStream         *m_stream  = nullptr;
    std::atomic<bool> m_playing{false};

    std::vector<int16_t> m_buffer;
    std::atomic<int>     m_playPos{0};
    std::mutex           m_bufMutex;
};
