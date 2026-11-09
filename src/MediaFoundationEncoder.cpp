#include "MediaFoundationEncoder.h"
#include "encoding/AudioSampleTiming.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/MFSampleTimes_win.h"
#include "encoding/VideoBitrate.h"
#include "encoding/VideoRateControl.h"

#ifdef Q_OS_WIN

#include "capture/D3D11Frame.h"
#include "encoding/D3D11Nv12Converter_win.h"

#include <QDir>
#include <QFile>
#include <QDebug>
#include <QHash>
#include <QThread>
#include <QUuid>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <wrl/client.h>

#include <mutex>

#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

namespace {

// Frames the GPU probe encodes before checking that the file decodes.
constexpr int kGpuProbeFrames = 3;
// Probe frames are cleared to 50% gray; decoded luma must come back near it
// (16 + 0.5 * 219 in BT.709 limited range).
constexpr int kGpuProbeLuma = 126;
constexpr int kGpuProbeLumaTolerance = 24;

// The GPU path converts with an explicit matrix; tag the stream with it so
// players do not guess (BT.601 is the usual guess for unknown streams).
HRESULT setBt709LimitedRange(IMFMediaType *type)
{
    HRESULT hr = type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
    return hr;
}

QString hrText(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QChar('0'));
}

} // namespace

class MediaFoundationEncoderPrivate
{
public:
    IMFSinkWriter *sinkWriter = nullptr;
    DWORD videoStreamIndex = 0;
    bool enableHardwareTransforms = true;

    // GPU input (#155): frames arrive as textures on the capture device and
    // are converted to NV12 surfaces there; the sink writer shares the device.
    std::shared_ptr<SnapTray::D3D11FrameDevice> nativeDevice;
    std::unique_ptr<SnapTray::D3D11Nv12Converter> converter;  // set while GPU input is in use
    ComPtr<IMFDXGIDeviceManager> deviceManager;
    bool gpuProbe = false;  // this encoder is the capability probe itself
    QString gpuUnavailableReason;  // why start() chose CPU input despite a device
    bool warnedAboutGpuFailure = false;
    bool gpuInput() const { return converter != nullptr; }

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

    // For the intermediate (ConstantQuality requested) this is both the
    // average target and the peak of the peak-constrained VBR encode.
    UINT32 calculateBitrate() const {
        if (requestedRateControl == SnapTray::VideoRateControl::ConstantQuality) {
            return static_cast<UINT32>(SnapTray::IntermediateQuality::intermediateBitrate(frameSize, frameRate));
        }
        return static_cast<UINT32>(SnapTray::VideoBitrate::forQuality(frameSize, frameRate, quality));
    }

    HRESULT createSinkWriter(const QString &path) {
        IMFAttributes *attributes = nullptr;
        HRESULT hr = MFCreateAttributes(&attributes, 2);
        if (FAILED(hr)) return hr;

        // Enable hardware transforms for better performance
        hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, enableHardwareTransforms ? TRUE : FALSE);
        if (SUCCEEDED(hr) && deviceManager) {
            // The encoder MFT takes surfaces on this device instead of memory buffers.
            hr = attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, deviceManager.Get());
        }
        if (FAILED(hr)) {
            attributes->Release();
            return hr;
        }

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

        if (gpuInput()) {
            hr = setBt709LimitedRange(outputType);
            if (FAILED(hr)) goto done;
        }

        hr = sinkWriter->AddStream(outputType, &videoStreamIndex);
        if (FAILED(hr)) goto done;

        // ========== Input type (RGB32, or NV12 surfaces on the GPU path) ==========
        hr = MFCreateMediaType(&inputType);
        if (FAILED(hr)) goto done;

        hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr)) goto done;

        hr = inputType->SetGUID(MF_MT_SUBTYPE, gpuInput() ? MFVideoFormat_NV12 : MFVideoFormat_RGB32);
        if (FAILED(hr)) goto done;

        if (gpuInput()) {
            hr = setBt709LimitedRange(inputType);
            if (FAILED(hr)) goto done;
        }

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
        // but ignored by the H.264 MFT. When the full set is rejected, keep
        // the GOP (seeking in the preview depends on it) before giving up on
        // parameters altogether.
        //
        // A ConstantQuality request (the preview intermediate) is encoded as
        // peak-constrained VBR at the intermediate ceiling: the MFT's Quality
        // mode accepts CODECAPI_AVEncCommonMaxBitRate but ignores it (random
        // content measured at 14.5x the ceiling), so it cannot be bounded on
        // this platform. effectiveRateControl therefore stays Bitrate.
        {
            const bool intermediate = requestedRateControl == SnapTray::VideoRateControl::ConstantQuality;
            const UINT32 gopFrames = keyFrameIntervalSeconds > 0
                ? static_cast<UINT32>(frameRate * keyFrameIntervalSeconds) : 0;
            // full: GOP plus, for the intermediate, peak-constrained VBR;
            // otherwise: GOP plus unconstrained VBR.
            auto createParams = [this, intermediate, gopFrames](bool full) -> IMFAttributes* {
                IMFAttributes *params = nullptr;
                HRESULT paramHr = MFCreateAttributes(&params, 5);
                if (FAILED(paramHr) || !params) {
                    qWarning() << "MediaFoundationEncoder: cannot create encoding parameters (hr =" << Qt::hex << paramHr << ")";
                    return nullptr;
                }
                if (gopFrames > 0) {
                    paramHr = params->SetUINT32(CODECAPI_AVEncMPVGOPSize, gopFrames);
                    if (FAILED(paramHr)) {
                        qWarning() << "MediaFoundationEncoder: GOP size parameter rejected (hr =" << Qt::hex << paramHr << ")";
                    }
                }
                if (full && intermediate) {
                    const HRESULT modeHr = params->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
                        eAVEncCommonRateControlMode_PeakConstrainedVBR);
                    const HRESULT peakHr = params->SetUINT32(CODECAPI_AVEncCommonMaxBitRate, calculateBitrate());
                    const HRESULT meanHr = params->SetUINT32(CODECAPI_AVEncCommonMeanBitRate, calculateBitrate());
                    // Bound the initial burst as well as the drain rate. The
                    // default buffer can overshoot the ceiling on short clips.
                    constexpr UINT32 kBufferDurationMs = 500;
                    const UINT32 bufferBytes = static_cast<UINT32>(
                        quint64(calculateBitrate()) * kBufferDurationMs / (8 * 1000));
                    const HRESULT bufferHr = params->SetUINT32(CODECAPI_AVEncCommonBufferSize, bufferBytes);
                    if (FAILED(modeHr) || FAILED(peakHr) || FAILED(meanHr) || FAILED(bufferHr)) {
                        qWarning() << "MediaFoundationEncoder: peak-constrained VBR parameters rejected (hr =" << Qt::hex
                                   << modeHr << peakHr << meanHr << bufferHr << ")";
                    }
                } else if (!full) {
                    paramHr = params->SetUINT32(CODECAPI_AVEncCommonRateControlMode,
                        eAVEncCommonRateControlMode_UnconstrainedVBR);
                    if (FAILED(paramHr)) {
                        qWarning() << "MediaFoundationEncoder: VBR mode parameter rejected (hr =" << Qt::hex << paramHr << ")";
                    }
                }
                return params;
            };

            const char *appliedParams = "encoder defaults";
            bool peakApplied = false;
            if (intermediate || gopFrames > 0) {
                encodingParams = createParams(true);
            }
            hr = sinkWriter->SetInputMediaType(videoStreamIndex, inputType, encodingParams);
            if (encodingParams) {
                encodingParams->Release();
                encodingParams = nullptr;
                if (SUCCEEDED(hr)) {
                    appliedParams = intermediate ? "GOP + peak-constrained VBR" : "GOP";
                    peakApplied = intermediate;
                } else {
                    qWarning() << "MediaFoundationEncoder: encoding parameters rejected (hr =" << Qt::hex << hr
                               << "), retrying with GOP and unconstrained VBR";
                    encodingParams = createParams(false);
                    hr = sinkWriter->SetInputMediaType(videoStreamIndex, inputType, encodingParams);
                    if (SUCCEEDED(hr)) {
                        appliedParams = "GOP + unconstrained VBR";
                    } else {
                        qWarning() << "MediaFoundationEncoder: GOP and VBR parameters rejected (hr =" << Qt::hex << hr
                                   << "), retrying with encoder defaults";
                        hr = sinkWriter->SetInputMediaType(videoStreamIndex, inputType, nullptr);
                        if (FAILED(hr)) {
                            qWarning() << "MediaFoundationEncoder: input type rejected with encoder defaults (hr ="
                                       << Qt::hex << hr << ")";
                        }
                    }
                }
            }
            effectiveRateControl = SnapTray::VideoRateControl::Bitrate;
            if (SUCCEEDED(hr)) {
                qDebug() << "MediaFoundationEncoder: video encoding parameters applied:" << appliedParams;
                if (peakApplied) {
                    qDebug() << "MediaFoundationEncoder: intermediate VBR at" << calculateBitrate() << "bps, peak"
                             << calculateBitrate() << "bps, GOP" << gopFrames << "frames";
                }
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
        disableGpuInput();
        running = false;
    }

    // Sink writer, streams and BeginWriting, for the input chosen by gpuInput().
    QString openSinkWriter(const QString &path) {
        HRESULT hr = createSinkWriter(path);
        if (FAILED(hr)) {
            return QString("Failed to create sink writer: %1").arg(hrText(hr));
        }

        hr = configureVideoStream();
        if (FAILED(hr)) {
            cleanup();
            return QString("Failed to configure video stream: %1").arg(hrText(hr));
        }

        // Configure audio stream if enabled
        if (audioEnabled) {
            hr = configureAudioStream();
            if (FAILED(hr)) {
                qWarning() << "MediaFoundationEncoder: Failed to configure audio stream: 0x" << Qt::hex << hr
                           << "- continuing without audio";
                audioEnabled = false;
            } else {
                qDebug() << "MediaFoundationEncoder: Audio stream configured -"
                         << audioSampleRate << "Hz," << audioChannels << "ch,"
                         << audioBitsPerSample << "bit";
            }
        }

        // CPU frames go in as RGB32 through a video processor that would
        // otherwise re-time them to the nominal frame rate (and hold back the
        // last one): each frame keeps the time it was captured at (#210).
        // NV12 GPU input has no processor in between.
        if (!gpuInput()) {
            hr = SnapTray::keepSampleTimesThroughVideoProcessor(sinkWriter, videoStreamIndex);
            if (FAILED(hr)) {
                qWarning() << "MediaFoundationEncoder: cannot keep frame times through the video processor:"
                           << hrText(hr);
            }
        }

        hr = sinkWriter->BeginWriting();
        if (FAILED(hr)) {
            cleanup();
            return QString("Failed to begin writing: %1").arg(hrText(hr));
        }
        return {};
    }

    // Converter and device manager for GPU input; empty on success.
    QString enableGpuInput() {
        auto gpuConverter = std::make_unique<SnapTray::D3D11Nv12Converter>();
        QString error;
        if (!gpuConverter->initialize(nativeDevice, frameSize, frameRate, &error)) {
            return error;
        }
        UINT resetToken = 0;
        HRESULT hr = MFCreateDXGIDeviceManager(&resetToken, &deviceManager);
        if (SUCCEEDED(hr)) {
            hr = deviceManager->ResetDevice(nativeDevice->device.Get(), resetToken);
        }
        if (FAILED(hr)) {
            deviceManager.Reset();
            return QString("DXGI device manager unavailable: %1").arg(hrText(hr));
        }
        converter = std::move(gpuConverter);
        return {};
    }

    void disableGpuInput() {
        converter.reset();
        deviceManager.Reset();
    }

    // The sink writer only saves CPU work when its encoder takes the surfaces
    // itself; a software encoder would read every surface back. Empty when the
    // video encoder is D3D11 aware.
    QString checkEncoderUsesSurfaces() const {
        ComPtr<IMFSinkWriterEx> writerEx;
        if (FAILED(sinkWriter->QueryInterface(IID_PPV_ARGS(&writerEx)))) {
            return QStringLiteral("sink writer does not expose its transforms");
        }
        for (DWORD index = 0;; ++index) {
            GUID category = GUID_NULL;
            ComPtr<IMFTransform> transform;
            if (FAILED(writerEx->GetTransformForStream(videoStreamIndex, index, &category, &transform))) {
                break;
            }
            if (category != MFT_CATEGORY_VIDEO_ENCODER) continue;
            ComPtr<IMFAttributes> attributes;
            UINT32 d3d11Aware = FALSE;
            if (SUCCEEDED(transform->GetAttributes(&attributes))) {
                attributes->GetUINT32(MF_SA_D3D11_AWARE, &d3d11Aware);
            }
            return d3d11Aware ? QString() : QStringLiteral("the selected H.264 encoder does not take D3D11 surfaces");
        }
        return QStringLiteral("no H.264 encoder transform found");
    }

    // Converted (or failed) GPU sample to the sink writer; true when written.
    bool writeGpuSample(HRESULT convertHr, IMFSample *sample, qint64 timestampMs) {
        HRESULT hr = convertHr;
        if (SUCCEEDED(hr)) {
            const LONGLONG timestamp = timestampMs >= 0 ? timestampMs * 10000LL
                                                        : frameNumber * frameDuration100ns();
            hr = sample->SetSampleTime(timestamp);
            if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(frameDuration100ns());
            if (SUCCEEDED(hr)) hr = sinkWriter->WriteSample(videoStreamIndex, sample);
        }
        if (FAILED(hr)) {
            // The stream keeps its configuration: a lost device rejects the
            // remaining frames instead of switching encoders mid-stream.
            if (!warnedAboutGpuFailure) {
                const HRESULT removed = nativeDevice->device->GetDeviceRemovedReason();
                qWarning() << "MediaFoundationEncoder: GPU frame rejected:" << hrText(hr)
                           << (FAILED(removed) ? QString("device lost (%1)").arg(hrText(removed)) : QString());
                warnedAboutGpuFailure = true;
            }
            return false;
        }
        framesWritten++;
        frameNumber++;
        return true;
    }

    static bool probeGpuInput(const std::shared_ptr<SnapTray::D3D11FrameDevice> &device,
                              const QSize &size, int frameRate, QString *reason);
};

namespace {

// Empty when `path` holds a video stream of `size` that decodes to the probe's
// mid-gray. The reader is asked for uncompressed NV12, so a decoder has to
// run: without an output type ReadSample hands back the compressed samples.
QString verifyDecodes(const QString &path, const QSize &size)
{
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, &reader);
    if (FAILED(hr)) return QString("probe output unreadable: %1").arg(hrText(hr));
    const DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    ComPtr<IMFMediaType> type;
    UINT32 width = 0;
    UINT32 height = 0;
    if (FAILED(reader->GetNativeMediaType(stream, 0, &type))
        || FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height))
        || QSize(int(width), int(height)) != size) {
        return QString("probe output has the wrong frame size %1x%2").arg(width).arg(height);
    }
    ComPtr<IMFMediaType> uncompressed;
    hr = MFCreateMediaType(&uncompressed);
    if (SUCCEEDED(hr)) hr = uncompressed->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = uncompressed->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(stream, nullptr, uncompressed.Get());
    if (FAILED(hr)) return QString("no decoder for the probe output: %1").arg(hrText(hr));

    for (int attempt = 0; attempt < kGpuProbeFrames * 4; ++attempt) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(stream, 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) return QString("probe output does not decode: %1").arg(hrText(hr));
        if (sample) {
            ComPtr<IMFMediaBuffer> buffer;
            BYTE *data = nullptr;
            DWORD length = 0;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || FAILED(buffer->Lock(&data, nullptr, &length))
                || length == 0) {
                return QStringLiteral("probe output decoded to an empty frame");
            }
            // The first luma sample of a frame cleared to 50% gray: about 126
            // in BT.709 limited range. Far off means the encoder wrote garbage.
            const int luma = data[0];
            buffer->Unlock();
            if (qAbs(luma - kGpuProbeLuma) > kGpuProbeLumaTolerance) {
                return QString("probe output decoded to the wrong picture (luma %1)").arg(luma);
            }
            return {};
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
    }
    return QStringLiteral("probe output has no decodable frame");
}

} // namespace

bool MediaFoundationEncoderPrivate::probeGpuInput(const std::shared_ptr<SnapTray::D3D11FrameDevice> &device,
                                                  const QSize &size, int frameRate, QString *reason)
{
    // One probe per adapter and stream shape per run: a driver that accepted
    // the surfaces once keeps doing so, and the probe costs a short encode.
    static std::mutex cacheMutex;
    static QHash<QString, QString> cache;  // key -> failure reason, empty = works
    const QString key = QString("%1:%2/%3x%4@%5")
                            .arg(device->adapterLuid.HighPart).arg(device->adapterLuid.LowPart)
                            .arg(size.width()).arg(size.height()).arg(frameRate);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto cached = cache.constFind(key);
        if (cached != cache.constEnd()) {
            *reason = cached.value();
            return reason->isEmpty();
        }
    }

    QString failure;
    const QString path = QDir::temp().filePath(
        QString("snaptray-gpu-probe-%1.mp4").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    {
        MediaFoundationEncoder probe(true, nullptr);
        probe.d->gpuProbe = true;
        probe.setNativeFrameDevice(device);
        auto pool = SnapTray::D3D11TexturePool::create(device, size);
        if (!probe.start(path, size, frameRate)) {
            failure = probe.lastError();
        } else if (!probe.acceptsNativeFrames()) {
            failure = probe.d->gpuUnavailableReason;
        } else if (!pool) {
            failure = QStringLiteral("cannot allocate probe textures");
        } else {
            for (int i = 0; i < kGpuProbeFrames; ++i) {
                auto frame = pool->acquire();
                ComPtr<ID3D11RenderTargetView> target;
                if (frame && SUCCEEDED(device->device->CreateRenderTargetView(frame->texture.Get(), nullptr, &target))) {
                    const float gray[4] = {0.5f, 0.5f, 0.5f, 1.0f};
                    device->context->ClearRenderTargetView(target.Get(), gray);
                }
                probe.writeNativeFrame(frame, qint64(i) * 1000 / frameRate);
            }
            bool finalized = false;
            QObject::connect(&probe, &IVideoEncoder::finished, &probe,
                             [&finalized](bool success, const QString &) { finalized = success; });
            if (probe.framesWritten() != kGpuProbeFrames) {
                failure = QStringLiteral("probe frames were rejected");
                probe.abort();
            } else {
                probe.finish();
                failure = finalized ? verifyDecodes(path, probe.d->frameSize)
                                    : QString("probe encode did not finalize: %1").arg(probe.lastError());
            }
        }
    }
    QFile::remove(path);

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache.insert(key, failure);
    }
    *reason = failure;
    return failure.isEmpty();
}

MediaFoundationEncoder::MediaFoundationEncoder(QObject *parent)
    : MediaFoundationEncoder(true, parent)
{
}

MediaFoundationEncoder::MediaFoundationEncoder(bool enableHardwareTransforms, QObject *parent)
    : IVideoEncoder(parent)
    , d(new MediaFoundationEncoderPrivate)
{
    d->enableHardwareTransforms = enableHardwareTransforms;
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
    d->warnedAboutGpuFailure = false;
    d->gpuUnavailableReason.clear();

    // Remove existing file if present
    QFile::remove(outputPath);

    // GPU input first, when the capture device allows it and a real encode
    // on this adapter worked; any failure keeps the CPU input below.
    if (d->nativeDevice) {
        const bool audioRequested = d->audioEnabled;
        QString reason;
        if (!d->enableHardwareTransforms) {
            reason = QStringLiteral("software encoding requested");
        } else if (!d->nativeDevice->videoSupport || d->nativeDevice->isLost()) {
            reason = QStringLiteral("capture device has no usable video support");
        } else if (d->gpuProbe
                   || MediaFoundationEncoderPrivate::probeGpuInput(d->nativeDevice, d->frameSize, d->frameRate, &reason)) {
            reason = d->enableGpuInput();
            if (reason.isEmpty()) {
                reason = d->openSinkWriter(outputPath);
                if (reason.isEmpty()) {
                    reason = d->checkEncoderUsesSurfaces();
                    if (!reason.isEmpty()) d->cleanup();
                }
                if (!reason.isEmpty()) {
                    d->disableGpuInput();
                    // The CPU attempt configures audio afresh.
                    d->audioEnabled = audioRequested;
                    QFile::remove(outputPath);
                }
            }
        }
        if (!reason.isEmpty()) {
            d->gpuUnavailableReason = reason;
            qWarning() << "MediaFoundationEncoder: GPU input unavailable on" << d->nativeDevice->adapterName
                       << "-" << reason << "- using CPU frames";
        }
    }

    if (!d->sinkWriter) {
        d->lastError = d->openSinkWriter(outputPath);
        if (!d->lastError.isEmpty()) {
            return false;
        }
    }

    d->running = true;
    qDebug() << "MediaFoundationEncoder: Started encoding to" << outputPath
             << "size:" << d->frameSize << "fps:" << frameRate
             << "quality:" << d->quality << "bitrate:" << d->calculateBitrate()
             << "input:" << (d->gpuInput() ? QString("GPU NV12 on %1").arg(d->nativeDevice->adapterName)
                                           : QStringLiteral("CPU RGB32"));
    return true;
}

void MediaFoundationEncoder::setNativeFrameDevice(std::shared_ptr<void> device)
{
    if (d->running) {
        qWarning() << "MediaFoundationEncoder: Cannot change the native frame device while running";
        return;
    }
    d->nativeDevice = std::static_pointer_cast<SnapTray::D3D11FrameDevice>(device);
}

bool MediaFoundationEncoder::acceptsNativeFrames() const
{
    return d->running && d->gpuInput();
}

bool MediaFoundationEncoder::writeNativeFrame(const std::shared_ptr<void> &nativeBuffer, qint64 timestampMs)
{
    if (!d->running || !d->sinkWriter) {
        return false;
    }
    const auto frame = SnapTray::d3d11FrameFrom(nativeBuffer);
    if (!frame || !frame->texture) {
        return false;
    }
    if (!d->gpuInput() || frame->device != d->converter->device()) {
        // CPU input, or a frame from another device (the capture device was
        // recreated): the same stream continues through a CPU copy.
        const QImage image = SnapTray::readBackD3D11Frame(*frame);
        if (image.isNull()) {
            return false;
        }
        writeFrame(image, timestampMs);
        return true;
    }
    ComPtr<IMFSample> sample;
    const HRESULT hr = d->converter->convert(frame->texture.Get(), frame->size, &sample);
    // A failed GPU write is rejected like a failed WriteSample on the CPU
    // path: retrying it as an image would not succeed on a lost device.
    if (d->writeGpuSample(hr, sample.Get(), timestampMs)) {
        emit progress(d->framesWritten);
    }
    return true;
}

void MediaFoundationEncoder::writeFrame(const QImage &frame, qint64 timestampMs)
{
    if (!d->running || !d->sinkWriter) {
        return;
    }

    if (d->gpuInput()) {
        // The stream takes NV12 surfaces: upload and convert on the GPU.
        ComPtr<IMFSample> gpuSample;
        const HRESULT hr = d->converter->convertImage(frame, &gpuSample);
        if (d->writeGpuSample(hr, gpuSample.Get(), timestampMs)) {
            emit progress(d->framesWritten);
        }
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
