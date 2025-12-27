#include "MockVideoEncoder.h"

MockVideoEncoder::MockVideoEncoder(QObject *parent)
    : IVideoEncoder(parent)
{
}

bool MockVideoEncoder::start(const QString &outputPath, const QSize &frameSize, int frameRate)
{
    m_startCalls++;
    m_startOutputPath = outputPath;
    m_startFrameSize = frameSize;
    m_startFrameRate = frameRate;

    if (!m_startSucceeds) {
        m_lastError = QStringLiteral("Mock start failure");
        emit error(m_lastError);
        return false;
    }

    m_outputPath = outputPath;
    m_running = true;
    m_framesWritten = 0;
    m_writtenFrames.clear();
    m_writtenFrameTimestamps.clear();
    m_writtenAudioSamples.clear();

    return true;
}

void MockVideoEncoder::writeFrame(const QImage &frame, qint64 timestampMs)
{
    m_writeFrameCalls++;

    if (!m_running) {
        return;
    }

    m_writtenFrames.append(frame);
    m_writtenFrameTimestamps.append(timestampMs);
    m_framesWritten++;

    emit progress(m_framesWritten);
}

void MockVideoEncoder::finish()
{
    m_finishCalls++;

    if (!m_running) {
        return;
    }

    m_running = false;

    if (m_finishSucceeds) {
        emit finished(true, m_outputPath);
    } else {
        m_lastError = QStringLiteral("Mock finish failure");
        emit error(m_lastError);
        emit finished(false, m_outputPath);
    }
}

void MockVideoEncoder::abort()
{
    m_abortCalls++;

    if (!m_running) {
        return;
    }

    m_running = false;
    m_writtenFrames.clear();
    m_writtenFrameTimestamps.clear();
    m_writtenAudioSamples.clear();

    emit finished(false, m_outputPath);
}

void MockVideoEncoder::setQuality(int quality)
{
    m_quality = quality;
}

void MockVideoEncoder::setAudioFormat(int sampleRate, int channels, int bitsPerSample)
{
    m_audioSampleRate = sampleRate;
    m_audioChannels = channels;
    m_audioBitsPerSample = bitsPerSample;
}

void MockVideoEncoder::writeAudioSamples(const QByteArray &pcmData, qint64 timestampMs)
{
    m_writeAudioCalls++;

    if (!m_running) {
        return;
    }

    m_writtenAudioSamples.append(qMakePair(pcmData, timestampMs));
}

void MockVideoEncoder::setAvailable(bool available)
{
    m_available = available;
}

void MockVideoEncoder::setStartSucceeds(bool succeeds)
{
    m_startSucceeds = succeeds;
}

void MockVideoEncoder::setFinishSucceeds(bool succeeds)
{
    m_finishSucceeds = succeeds;
}

void MockVideoEncoder::setAudioSupported(bool supported)
{
    m_audioSupported = supported;
}

void MockVideoEncoder::simulateFinished(bool success)
{
    m_running = false;
    emit finished(success, m_outputPath);
}

void MockVideoEncoder::simulateError(const QString &message)
{
    m_lastError = message;
    emit error(message);
}

void MockVideoEncoder::simulateProgress(qint64 frames)
{
    m_framesWritten = frames;
    emit progress(frames);
}

void MockVideoEncoder::resetCounters()
{
    m_startCalls = 0;
    m_finishCalls = 0;
    m_abortCalls = 0;
    m_writeFrameCalls = 0;
    m_writeAudioCalls = 0;
    m_framesWritten = 0;
    m_writtenFrames.clear();
    m_writtenFrameTimestamps.clear();
    m_writtenAudioSamples.clear();
}
