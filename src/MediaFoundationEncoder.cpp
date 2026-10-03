#include "MediaFoundationEncoder.h"
#include "encoding/AudioSampleTiming.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoBitrate.h"
#include "encoding/VideoRateControl.h"

#ifdef Q_OS_WIN

#include <QFile>
#include <QDebug>
#include <QThread>

#include <windows.h>
#include <oleauto.h>
#include <icodecapi.h>
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
    SnapTray::VideoRateControl requestedRateControl = SnapTray::VideoRateControl::Bitrate;
    SnapTray::VideoRateControl effectiveRateControl = SnapTray::VideoRateControl::Bitrate;
    int keyFrameIntervalSeconds = 0; // 0 = encoder default
    bool mfInitialized = false;
    bool warnedAboutFrameResize = false;

    LONGLONG frameDuration100ns() const {
        return frameRate > 0 ? 10000000LL / frameRate : 10000000LL / 30;
    }

    // In constant-quality mode MF_MT_AVG_BITRATE is the VBR ceiling (and the
    // whole budget if the MFT refuses quality mode).
    UINT32 calculateBitrate() const {
        if (requestedRateControl == SnapTray::VideoRateControl::ConstantQuality) {
            return static_cast<UINT32>(SnapTray::IntermediateQuality::intermediateBitrate(frameSize, frameRate));
        }
        return static_cast<UINT32>(SnapTray::VideoBitrate::forQuality(frameSize, frameRate, quality));
    }

    // Reads the rate-control mode back from the encoder MFT (it only exists
    // after SetInputMediaType); only a confirmed Quality mode is reported as
    // ConstantQuality. Never fails the recording.
    void verifyRateControl(bool qualityParamsSet) {
        effectiveRateControl = SnapTray::VideoRateControl::Bitrate;
        if (requestedRateControl != SnapTray::VideoRateControl::ConstantQuality) return;
        if (!qualityParamsSet) {
            qWarning() << "MediaFoundationEncoder: constant quality not applied, using VBR at" << calculateBitrate() << "bps";
            return;
        }
        ICodecAPI *codecApi = nullptr;
        HRESULT hr = sinkWriter->GetServiceForStream(videoStreamIndex, GUID_NULL, IID_PPV_ARGS(&codecApi));
        if (FAILED(hr) || !codecApi) {
            qWarning() << "MediaFoundationEncoder: ICodecAPI unavailable, cannot confirm constant quality (hr ="
                       << Qt::hex << hr << ")";
            return;
        }
        VARIANT value;
        VariantInit(&value);
        hr = codecApi->GetValue(&CODECAPI_AVEncCommonRateControlMode, &value);
        if (SUCCEEDED(hr) && value.vt == VT_UI4 && value.ulVal == eAVEncCommonRateControlMode_Quality) {
            effectiveRateControl = SnapTray::VideoRateControl::ConstantQuality;
            qDebug() << "MediaFoundationEncoder: constant quality" << quality << "confirmed";
        } else {
            qWarning() << "MediaFoundationEncoder: quality mode not adopted (hr =" << Qt::hex << hr
                       << "), falling back to VBR at" << calculateBitrate() << "bps";
            VariantClear(&value);
            VariantInit(&value);
            value.vt = VT_UI4;
            value.ulVal = eAVEncCommonRateControlMode_UnconstrainedVBR;
            (void)codecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &value);
        }
        VariantClear(&value);
        codecApi->Release();
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
        IMFAttributes *encodingParams = nullptr;
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

        // Codec properties must be handed over as encoding parameters: values
        // set through ICodecAPI after the input type is negotiated are accepted
        // but ignored by the H.264 MFT.
        {
            const bool wantQuality = requestedRateControl == SnapTray::VideoRateControl::ConstantQuality;
            bool qualityParamsSet = false;
            if (wantQuality || keyFrameIntervalSeconds > 0) {
                HRESULT paramHr = MFCreateAttributes(&encodingParams, 3);
                if (FAILED(paramHr)) {
                    qWarning() << "MediaFoundationEncoder: cannot create encoding parameters (hr =" << Qt::hex << paramHr << ")";
                    encodingParams = nullptr;
                } else {
                    if (keyFrameIntervalSeconds > 0) {
                        paramHr = encodingParams->SetUINT32(CODECAPI_AVEncMPVGOPSize,
                            static_cast<UINT32>(frameRate * keyFrameIntervalSeconds));
                        if (FAILED(paramHr)) {
                            qWarning() << "MediaFoundationEncoder: GOP size parameter rejected (hr =" << Qt::hex << paramHr << ")";
                        }
                    }
                    if (wantQuality) {
                        const HRESULT modeHr = encodingParams->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
                            eAVEncCommonRateControlMode_Quality);
                        const HRESULT qualityHr = encodingParams->SetUINT32(CODECAPI_AVEncCommonQuality,
                            static_cast<UINT32>(quality));
                        qualityParamsSet = SUCCEEDED(modeHr) && SUCCEEDED(qualityHr);
                        if (!qualityParamsSet) {
                            qWarning() << "MediaFoundationEncoder: quality parameters rejected (hr =" << Qt::hex
                                       << modeHr << qualityHr << ")";
                        }
                    }
                }
            }
            hr = sinkWriter->SetInputMediaType(videoStreamIndex, inputType, encodingParams);
            if (FAILED(hr) && encodingParams) {
                qWarning() << "MediaFoundationEncoder: encoding parameters rejected (hr =" << Qt::hex << hr
                           << "), retrying with encoder defaults";
                qualityParamsSet = false;
                hr = sinkWriter->SetInputMediaType(videoStreamIndex, inputType, nullptr);
            }
            if (SUCCEEDED(hr)) {
                verifyRateControl(qualityParamsSet);
            }
        }

    done:
        if (outputType) outputType->Release();
        if (inputType) inputType->Release();
        if (encodingParams) encodingParams->Release();
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

    if ((frameSize.width() % 2 != 0) || (frameSize.height() % 2 != 0)) {
        qWarning() << "MediaFoundationEncoder: Received odd frame size from upstream:" << frameSize
                   << "- applying compatibility fallback.";
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
    d->frameRate = qBound(1, frameRate, 240);
    d->framesWritten = 0;
    d->frameNumber = 0;
    d->warnedAboutFrameResize = false;

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
        if (!d->warnedAboutFrameResize) {
            qWarning() << "MediaFoundationEncoder: Frame size mismatch, using fallback scaling."
                       << "incoming:" << frame.size() << "expected:" << d->frameSize;
            d->warnedAboutFrameResize = true;
        }
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

        HRESULT unlockHr = buffer->Unlock();
        if (FAILED(unlockHr)) {
            qWarning() << "MediaFoundationEncoder: Buffer Unlock failed:" << Qt::hex << unlockHr;
        }
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

    qDebug() << "MediaFoundationEncoder: Finalize on thread"
             << reinterpret_cast<quintptr>(QThread::currentThreadId());

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

void MediaFoundationEncoder::setRateControl(SnapTray::VideoRateControl mode, int qualityValue)
{
    d->requestedRateControl = mode;
    d->quality = qBound(0, qualityValue, 100);
}

void MediaFoundationEncoder::setKeyFrameIntervalSeconds(int seconds)
{
    d->keyFrameIntervalSeconds = qMax(0, seconds);
}

SnapTray::VideoRateControl MediaFoundationEncoder::effectiveRateControl() const
{
    return d->effectiveRateControl;
}

void MediaFoundationEncoder::setAudioFormat(int sampleRate, int channels, int bitsPerSample)
{
    if (d->running) {
        qDebug() << "MediaFoundationEncoder: Cannot set audio format while running";
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

void MediaFoundationEncoder::writeAudioSamples(const QByteArray &pcmData, qint64 startFrame)
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
        HRESULT unlockHr = buffer->Unlock();
        if (FAILED(unlockHr)) {
            qWarning() << "MediaFoundationEncoder: Audio buffer Unlock failed:" << Qt::hex << unlockHr;
        }
    }

    hr = buffer->SetCurrentLength(bufferSize);
    if (FAILED(hr)) goto done;

    hr = MFCreateSample(&sample);
    if (FAILED(hr)) goto done;

    hr = sample->AddBuffer(buffer);
    if (FAILED(hr)) goto done;

    {
        // Quantize both absolute frame boundaries to Media Foundation's
        // 100-nanosecond clock so adjacent buffers cannot overlap or leave gaps.
        int bytesPerFrame = d->audioChannels * d->audioBitsPerSample / 8;
        int numFrames = bufferSize / bytesPerFrame;
        const auto timing = SnapTray::Audio::scaleAudioSampleRange(
            startFrame, numFrames, d->audioSampleRate, 10000000LL);
        if (!timing.valid) {
            qWarning() << "MediaFoundationEncoder: Invalid audio sample timing";
            goto done;
        }
        hr = sample->SetSampleTime(timing.start);
        if (FAILED(hr)) goto done;
        hr = sample->SetSampleDuration(timing.duration);
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
