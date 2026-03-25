// SPDX-License-Identifier: GPL-3.0-or-later
#include "audiooutput.h"
#include <QDebug>
#include <cstring>
#include <algorithm>

static constexpr int k_outputRate = 48000;

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
{
    Pa_Initialize();
}

AudioOutput::~AudioOutput()
{
    close();
    Pa_Terminate();
}

bool AudioOutput::open(const QString &deviceName)
{
    close();

    int deviceIndex = Pa_GetDefaultOutputDevice();
    if (!deviceName.isEmpty()) {
        int count = Pa_GetDeviceCount();
        for (int i = 0; i < count; ++i) {
            const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
            if (info && info->maxOutputChannels > 0) {
                if (QString::fromUtf8(info->name).contains(deviceName, Qt::CaseInsensitive)) {
                    deviceIndex = i;
                    break;
                }
            }
        }
    }

    if (deviceIndex == paNoDevice) {
        emit error(QStringLiteral("No audio output device found"));
        return false;
    }

    PaStreamParameters params{};
    params.device                    = deviceIndex;
    params.channelCount              = 1;  // mono output
    params.sampleFormat              = paInt16;
    params.suggestedLatency          = Pa_GetDeviceInfo(deviceIndex)->defaultLowOutputLatency;
    params.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(
        &m_stream,
        nullptr,         // no input
        &params,
        k_outputRate,
        256,
        paClipOff,
        &AudioOutput::paCallback,
        this);

    if (err != paNoError) {
        emit error(QStringLiteral("Failed to open audio output: %1")
                   .arg(QString::fromLatin1(Pa_GetErrorText(err))));
        m_stream = nullptr;
        return false;
    }

    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        emit error(QStringLiteral("Failed to start audio output: %1")
                   .arg(QString::fromLatin1(Pa_GetErrorText(err))));
        return false;
    }

    return true;
}

void AudioOutput::close()
{
    if (m_stream) {
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }
}

void AudioOutput::play(std::vector<int16_t> samples)
{
    std::lock_guard<std::mutex> lk(m_bufMutex);
    m_buffer = std::move(samples);
    m_playPos.store(0);
    m_playing.store(true);
    emit playbackStarted();
}

int AudioOutput::paCallback(const void * /*input*/, void *output,
                             unsigned long frameCount,
                             const PaStreamCallbackTimeInfo * /*ti*/,
                             PaStreamCallbackFlags /*flags*/,
                             void *userData)
{
    auto *self = static_cast<AudioOutput *>(userData);
    auto *out  = static_cast<int16_t *>(output);

    if (!self->m_playing.load()) {
        std::memset(out, 0, frameCount * sizeof(int16_t));
        return paContinue;
    }

    std::lock_guard<std::mutex> lk(self->m_bufMutex);
    int pos  = self->m_playPos.load();
    int avail = static_cast<int>(self->m_buffer.size()) - pos;
    int n     = static_cast<int>(frameCount);

    if (avail <= 0) {
        std::memset(out, 0, frameCount * sizeof(int16_t));
        if (self->m_playing.exchange(false))
            QMetaObject::invokeMethod(self, &AudioOutput::playbackFinished,
                                      Qt::QueuedConnection);
        return paContinue;
    }

    int copy = std::min(n, avail);
    std::memcpy(out, self->m_buffer.data() + pos, copy * sizeof(int16_t));
    if (copy < n)
        std::memset(out + copy, 0, (n - copy) * sizeof(int16_t));

    self->m_playPos.store(pos + copy);

    if (pos + copy >= static_cast<int>(self->m_buffer.size())) {
        if (self->m_playing.exchange(false))
            QMetaObject::invokeMethod(self, &AudioOutput::playbackFinished,
                                      Qt::QueuedConnection);
    }

    return paContinue;
}

QStringList AudioOutput::availableDevices()
{
    QStringList names;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0)
            names.append(QString::fromUtf8(info->name));
    }
    return names;
}
