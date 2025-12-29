#include "MediaFoundationEncoder.h"

#ifdef Q_OS_WIN

#include <QFile>
#include <QDebug>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>

#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

class MediaFoundationEncoderPrivate
{
public:
    IMFSinkWriter *sinkWriter = nullptr;
    DWORD videoStreamIndex = 0;

    // Audio members
    DWORD audioStreamIndex = 0;
    bool audioEnabled = false;
    int audioSampleRate = 48000;
    int audioChannels = 2;
    int audioBitsPerSample = 16;
    qint64 audioSamplesWritten = 0;

    QString outputPath;
    QString lastError;
    QSize frameSize;
    int frameRate = 30;
    qint64 framesWritten = 0;
    qint64 frameNumber = 0;
    bool running = false;
    int quality = 55;  // 0-100
    bool mfInitialized = false;

    LONGLONG frameDuration100ns() const {
        return 10000000LL / frameRate;
    }

    UINT32 calculateBitrate() const {
        int pixels = frameSize.width() * frameSize.height();
        // Quality 0-100 maps to bits per pixel 0.1-0.3
        double bitsPerPixel = 0.1 + (quality / 100.0) * 0.2;
        UINT32 bitrate = static_cast<UINT32>(pixels * frameRate * bitsPerPixel);
        // Clamp to reasonable range: 1 Mbps to 50 Mbps
        return qBound(1000000U, bitrate, 50000000U);
    }

    HRESULT createSinkWriter(const QString &path) {
        IMFAttributes *attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) return hr;

        // Enable hardware transforms for better performance
        hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

        hr = MFCreateSinkWriterFromURL(
            reinterpret_cast<LPCWSTR>(path.utf16()),
            nullptr,
            attributes,
            &sinkWriter
        );

        if (attributes) attributes->Release();
        return hr;
    }

    HRESULT configureVideoStream() {
        IMFMediaType *outputType = nullptr;
        IMFMediaType *inputType = nullptr;
        HRESULT hr = S_OK;

        // ========== Output type (H.264) ==========
        hr = MFCreateMediaType(&outputType);
        if (FAILED(hr)) goto done;

        hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) goto done;

        hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        if (FAILED(hr)) goto done;

        hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, calculateBitrate());
        if (FAILED(hr)) goto done;

        hr = outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(hr)) goto done;

        hr = MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, frameSize.width(), frameSize.height());
        if (FAILED(hr)) goto done;

        hr = MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, frameRate, 1);
        if (FAILED(hr)) goto done;

        hr = MFSetAttributeRatio(outputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(hr)) goto done;

        hr = sinkWriter->AddStream(outputType, &videoStreamIndex);
        if (FAILED(hr)) goto done;

        // ========== Input type (RGB32) ==========
        hr = MFCreateMediaType(&inputType);
        if (FAILED(hr)) goto done;

        hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) goto done;

        hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(hr)) goto done;

        hr = inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(hr)) goto done;

        hr = MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, frameSize.width(), frameSize.height());
        if (FAILED(hr)) goto done;

        hr = MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, frameRate, 1);
        if (FAILED(hr)) goto done;

        hr = MFSetAttributeRatio(inputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (FAILED(hr)) goto done;

        hr = sinkWriter->SetInputMediaType(videoStreamIndex, inputType, nullptr);

    done:
        if (outputType) outputType->Release();
        if (inputType) inputType->Release();
        return hr;
    }

    HRESULT configureAudioStream() {
        if (!audioEnabled) return S_OK;

        IMFMediaType *outputType = nullptr;
        IMFMediaType *inputType = nullptr;
        HRESULT hr = S_OK;

        // ========== Output type (AAC) ==========
        hr = MFCreateMediaType(&outputType);
        if (FAILED(hr)) goto done;

        hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(hr)) goto done;

        hr = outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (FAILED(hr)) goto done;

        hr = outputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audioSampleRate);
        if (FAILED(hr)) goto done;

        hr = outputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audioChannels);
        if (FAILED(hr)) goto done;

        hr = outputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (FAILED(hr)) goto done;

        // AAC bitrate: ~128kbps = 16000 bytes/sec
        hr = outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
        if (FAILED(hr)) goto done;

        hr = sinkWriter->AddStream(outputType, &audioStreamIndex);
        if (FAILED(hr)) goto done;

        // ========== Input type (PCM) ==========
        hr = MFCreateMediaType(&inputType);
        if (FAILED(hr)) goto done;

        hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(hr)) goto done;

        hr = inputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (FAILED(hr)) goto done;

        hr = inputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audioSampleRate);
        if (FAILED(hr)) goto done;

        hr = inputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audioChannels);
        if (FAILED(hr)) goto done;

        hr = inputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, audioBitsPerSample);
        if (FAILED(hr)) goto done;

        hr = inputType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,
            audioChannels * audioBitsPerSample / 8);
        if (FAILED(hr)) goto done;

        hr = inputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
            audioSampleRate * audioChannels * audioBitsPerSample / 8);
        if (FAILED(hr)) goto done;

        hr = sinkWriter->SetInputMediaType(audioStreamIndex, inputType, nullptr);

    done:
        if (outputType) outputType->Release();
        if (inputType) inputType->Release();
        return hr;
    }

    void cleanup() {
        if (sinkWriter) {
            sinkWriter->Release();
            sinkWriter = nullptr;
        }
        running = false;
    }
};

MediaFoundationEncoder::MediaFoundationEncoder(QObject *parent)
    : IVideoEncoder(parent)
    , d(new MediaFoundationEncoderPrivate)
{
    HRESULT hr = MFStartup(MF_VERSION);
    d->mfInitialized = SUCCEEDED(hr);
    if (!d->mfInitialized) {
        qWarning() << "MediaFoundationEncoder: Failed to initialize Media Foundation:" << Qt::hex << hr;
    }
}

MediaFoundationEncoder::~MediaFoundationEncoder()
{
    abort();
    if (d->mfInitialized) {
        MFShutdown();
    }
    delete d;
}

bool MediaFoundationEncoder::isAvailable() const
{
    return d->mfInitialized;
}

bool MediaFoundationEncoder::start(const QString &outputPath, const QSize &frameSize, int frameRate)
{
    if (d->running) {
        d->lastError = "Encoder already running";
        return false;
    }

    if (!d->mfInitialized) {
        d->lastError = "Media Foundation not initialized";
        return false;
    }

    // Ensure dimensions are even (required for H.264)
    QSize adjustedSize = frameSize;
    if (adjustedSize.width() % 2 != 0) {
        adjustedSize.setWidth(adjustedSize.width() + 1);
    }
    if (adjustedSize.height() % 2 != 0) {
        adjustedSize.setHeight(adjustedSize.height() + 1);
    }

    d->outputPath = outputPath;
    d->frameSize = adjustedSize;
    d->frameRate = frameRate;
    d->framesWritten = 0;
    d->frameNumber = 0;

    // Remove existing file if present
    QFile::remove(outputPath);

    HRESULT hr = d->createSinkWriter(outputPath);
    if (FAILED(hr)) {
        d->lastError = QString("Failed to create sink writer: 0x%1").arg(hr, 8, 16, QChar('0'));
        return false;
    }

    hr = d->configureVideoStream();
    if (FAILED(hr)) {
        d->lastError = QString("Failed to configure video stream: 0x%1").arg(hr, 8, 16, QChar('0'));
        d->cleanup();
        return false;
    }

    // Configure audio stream if enabled
    if (d->audioEnabled) {
        hr = d->configureAudioStream();
        if (FAILED(hr)) {
            qWarning() << "MediaFoundationEncoder: Failed to configure audio stream: 0x" << Qt::hex << hr
                       << "- continuing without audio";
            d->audioEnabled = false;
        } else {
            qDebug() << "MediaFoundationEncoder: Audio stream configured -"
                     << d->audioSampleRate << "Hz," << d->audioChannels << "ch,"
                     << d->audioBitsPerSample << "bit";
        }
    }

    hr = d->sinkWriter->BeginWriting();
    if (FAILED(hr)) {
        d->lastError = QString("Failed to begin writing: 0x%1").arg(hr, 8, 16, QChar('0'));
        d->cleanup();
        return false;
    }

    d->running = true;
    qDebug() << "MediaFoundationEncoder: Started encoding to" << outputPath
             << "size:" << d->frameSize << "fps:" << frameRate
             << "quality:" << d->quality << "bitrate:" << d->calculateBitrate();
    return true;
}

void MediaFoundationEncoder::writeFrame(const QImage &frame, qint64 timestampMs)
{
    if (!d->running || !d->sinkWriter) {
        return;
    }

    IMFSample *sample = nullptr;
    IMFMediaBuffer *buffer = nullptr;
    HRESULT hr = S_OK;

    // Scale frame if needed
    QImage scaledFrame = frame;
    if (frame.size() != d->frameSize) {
        scaledFrame = frame.scaled(d->frameSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    // Convert to RGB32 format
    QImage rgbFrame = scaledFrame.convertToFormat(QImage::Format_RGB32);

    DWORD bufferSize = static_cast<DWORD>(rgbFrame.sizeInBytes());
    hr = MFCreateMemoryBuffer(bufferSize, &buffer);
    if (FAILED(hr)) goto done;

    {
        BYTE *bufferData = nullptr;
        hr = buffer->Lock(&bufferData, nullptr, nullptr);
        if (FAILED(hr)) goto done;

        // Media Foundation expects bottom-up BGR, but QImage is top-down
        // We need to flip vertically
        const int stride = rgbFrame.bytesPerLine();
        for (int y = 0; y < d->frameSize.height(); y++) {
            const uchar *srcLine = rgbFrame.constScanLine(d->frameSize.height() - 1 - y);
            memcpy(bufferData + y * stride, srcLine, stride);
        }

        buffer->Unlock();
    }

    hr = buffer->SetCurrentLength(bufferSize);
    if (FAILED(hr)) goto done;

    hr = MFCreateSample(&sample);
    if (FAILED(hr)) goto done;

    hr = sample->AddBuffer(buffer);
    if (FAILED(hr)) goto done;

    {
        LONGLONG timestamp;
        if (timestampMs >= 0) {
            // Convert ms to 100-nanosecond units
            timestamp = timestampMs * 10000LL;
        } else {
            // Calculate from frame number
            timestamp = d->frameNumber * d->frameDuration100ns();
        }

        hr = sample->SetSampleTime(timestamp);
        if (FAILED(hr)) goto done;

        hr = sample->SetSampleDuration(d->frameDuration100ns());
        if (FAILED(hr)) goto done;
    }

    hr = d->sinkWriter->WriteSample(d->videoStreamIndex, sample);
    if (SUCCEEDED(hr)) {
        d->framesWritten++;
        d->frameNumber++;
        emit progress(d->framesWritten);
    } else {
        qWarning() << "MediaFoundationEncoder: WriteSample failed:" << Qt::hex << hr;
    }

done:
    if (sample) sample->Release();
    if (buffer) buffer->Release();
}

void MediaFoundationEncoder::finish()
{
    if (!d->running) {
        emit finished(false, QString());
        return;
    }

    d->running = false;
    QString outputPath = d->outputPath;
    qint64 framesWritten = d->framesWritten;

    HRESULT hr = d->sinkWriter->Finalize();
    d->cleanup();

    if (SUCCEEDED(hr)) {
        qDebug() << "MediaFoundationEncoder: Finished successfully, frames:" << framesWritten;
        emit finished(true, outputPath);
    } else {
        d->lastError = QString("Failed to finalize: 0x%1").arg(hr, 8, 16, QChar('0'));
        qWarning() << "MediaFoundationEncoder:" << d->lastError;
        emit error(d->lastError);
        emit finished(false, QString());
    }
}

void MediaFoundationEncoder::abort()
{
    if (!d->running) return;

    QString outputPath = d->outputPath;
    d->cleanup();

    // Remove partial output file
    QFile::remove(outputPath);
    qDebug() << "MediaFoundationEncoder: Aborted, removed" << outputPath;
}

bool MediaFoundationEncoder::isRunning() const
{
    return d->running;
}

QString MediaFoundationEncoder::lastError() const
{
    return d->lastError;
}

qint64 MediaFoundationEncoder::framesWritten() const
{
    return d->framesWritten;
}

QString MediaFoundationEncoder::outputPath() const
{
    return d->outputPath;
}

void MediaFoundationEncoder::setQuality(int quality)
{
    d->quality = qBound(0, quality, 100);
}

void MediaFoundationEncoder::setAudioFormat(int sampleRate, int channels, int bitsPerSample)
{
    if (d->running) {
        qWarning() << "MediaFoundationEncoder: Cannot set audio format while running";
        return;
    }
    d->audioEnabled = true;
    d->audioSampleRate = sampleRate;
    d->audioChannels = channels;
    d->audioBitsPerSample = bitsPerSample;
    qDebug() << "MediaFoundationEncoder: Audio format set -" << sampleRate << "Hz,"
             << channels << "ch," << bitsPerSample << "bit";
}

bool MediaFoundationEncoder::isAudioSupported() const
{
    return true;  // Media Foundation supports audio encoding
}

bool MediaFoundationEncoder::isAudioEnabled() const
{
    return d->audioEnabled;
}

void MediaFoundationEncoder::writeAudioSamples(const QByteArray &pcmData, qint64 timestampMs)
{
    if (!d->running || !d->sinkWriter || !d->audioEnabled) {
        return;
    }

    IMFSample *sample = nullptr;
    IMFMediaBuffer *buffer = nullptr;
    HRESULT hr = S_OK;

    DWORD bufferSize = static_cast<DWORD>(pcmData.size());
    hr = MFCreateMemoryBuffer(bufferSize, &buffer);
    if (FAILED(hr)) goto done;

    {
        BYTE *bufferData = nullptr;
        hr = buffer->Lock(&bufferData, nullptr, nullptr);
        if (FAILED(hr)) goto done;
        memcpy(bufferData, pcmData.constData(), bufferSize);
        buffer->Unlock();
    }

    hr = buffer->SetCurrentLength(bufferSize);
    if (FAILED(hr)) goto done;

    hr = MFCreateSample(&sample);
    if (FAILED(hr)) goto done;

    hr = sample->AddBuffer(buffer);
    if (FAILED(hr)) goto done;

    {
        // Convert ms to 100-nanosecond units
        LONGLONG timestamp = timestampMs * 10000LL;
        hr = sample->SetSampleTime(timestamp);
        if (FAILED(hr)) goto done;

        // Calculate duration based on number of samples
        int bytesPerFrame = d->audioChannels * d->audioBitsPerSample / 8;
        int numFrames = bufferSize / bytesPerFrame;
        LONGLONG duration = (static_cast<LONGLONG>(numFrames) * 10000000LL) / d->audioSampleRate;
        hr = sample->SetSampleDuration(duration);
        if (FAILED(hr)) goto done;
    }

    hr = d->sinkWriter->WriteSample(d->audioStreamIndex, sample);
    if (SUCCEEDED(hr)) {
        d->audioSamplesWritten++;
    } else {
        qWarning() << "MediaFoundationEncoder: WriteAudioSample failed: 0x" << Qt::hex << hr;
    }

done:
    if (sample) sample->Release();
    if (buffer) buffer->Release();
}

#endif // Q_OS_WIN
