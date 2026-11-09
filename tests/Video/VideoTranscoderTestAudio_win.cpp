#include "VideoTranscoderTestAudio.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QRect>
#include <QSize>

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <string>
#include <utility>

// Test-only Media Foundation decoders for tst_VideoTranscoder. Deliberately
// independent of MediaFoundationTranscoder_win.cpp so a shared mistake (frame
// orientation, timestamps) cannot cancel out between producer and checker.

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT32 kDecodeChannels = 2;
constexpr UINT32 kDecodeBitDepth = 16;
constexpr UINT32 kBitsPerByte = 8;
constexpr float kInt16Scale = 32768.0f;
constexpr double kHnsPerSecond = 10000000.0;
constexpr double kHnsPerMs = 10000.0;
constexpr int kBytesPerPixel = 4;
// Bound placement so a corrupt timestamp cannot request a huge allocation.
constexpr qint64 kMaxDecodedFrames = qint64(60) * 48000;
constexpr int kHresultHexDigits = 8;
constexpr int kHexBase = 16;

class MediaFoundationScope
{
public:
    MediaFoundationScope()
        : m_com(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
        , m_mf(MFStartup(MF_VERSION))
    {
    }
    ~MediaFoundationScope()
    {
        if (SUCCEEDED(m_mf)) {
            MFShutdown();
        }
        if (SUCCEEDED(m_com)) {
            CoUninitialize();
        }
    }
    MediaFoundationScope(const MediaFoundationScope&) = delete;
    MediaFoundationScope& operator=(const MediaFoundationScope&) = delete;

    bool ok() const { return (SUCCEEDED(m_com) || m_com == RPC_E_CHANGED_MODE) && SUCCEEDED(m_mf); }

private:
    HRESULT m_com;
    HRESULT m_mf;
};

QString hrMessage(const char* step, HRESULT hr)
{
    return QStringLiteral("%1 failed (0x%2)")
        .arg(QLatin1String(step))
        .arg(static_cast<quint32>(hr), kHresultHexDigits, kHexBase, QLatin1Char('0'));
}

HRESULT openReader(const QString& path, bool decodeVideo, ComPtr<IMFSourceReader>& reader)
{
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 2);
    if (SUCCEEDED(hr) && decodeVideo) {
        hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }
    if (SUCCEEDED(hr)) {
        hr = attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
    }
    const std::wstring native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
    if (SUCCEEDED(hr)) {
        hr = MFCreateSourceReaderFromURL(native.c_str(), attributes.Get(), &reader);
    }
    return hr;
}

HRESULT setPcmOutput(IMFSourceReader* reader, int sampleRate)
{
    ComPtr<IMFMediaType> pcm;
    HRESULT hr = MFCreateMediaType(&pcm);
    if (SUCCEEDED(hr)) hr = pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kDecodeBitDepth);
    if (FAILED(hr)) {
        return hr;
    }
    // Ask for the test's rate and layout first; fall back to the decoder's
    // native rate/channels (16-bit PCM) if the reader cannot convert.
    ComPtr<IMFMediaType> exact;
    hr = MFCreateMediaType(&exact);
    if (SUCCEEDED(hr)) hr = pcm->CopyAllItems(exact.Get());
    const UINT32 blockAlign = kDecodeChannels * kDecodeBitDepth / kBitsPerByte;
    if (SUCCEEDED(hr)) hr = exact->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(sampleRate));
    if (SUCCEEDED(hr)) hr = exact->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kDecodeChannels);
    if (SUCCEEDED(hr)) hr = exact->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
    if (SUCCEEDED(hr)) {
        hr = exact->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(sampleRate) * blockAlign);
    }
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, exact.Get());
    if (SUCCEEDED(hr)) {
        return hr;
    }
    return reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcm.Get());
}

struct FrameLayout {
    QSize size;
    QPoint origin; // displayed top-left inside the decoded frame
    LONG stride = 0;
};

HRESULT queryFrameLayout(IMFSourceReader* reader, FrameLayout* layout)
{
    ComPtr<IMFMediaType> type;
    HRESULT hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
    if (FAILED(hr)) {
        return hr;
    }
    UINT32 width = 0;
    UINT32 height = 0;
    hr = MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) {
        return hr;
    }
    layout->size = QSize(static_cast<int>(width), static_cast<int>(height));
    layout->origin = QPoint(0, 0);
    MFVideoArea aperture {};
    UINT32 blobSize = 0;
    if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&aperture),
                                sizeof(aperture), &blobSize))
        && blobSize == sizeof(aperture)) {
        layout->origin = QPoint(aperture.OffsetX.value, aperture.OffsetY.value);
    }
    UINT32 rawStride = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride))) {
        layout->stride = static_cast<LONG>(static_cast<INT32>(rawStride));
    } else {
        hr = MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &layout->stride);
    }
    return (SUCCEEDED(hr) && layout->stride == 0) ? MF_E_INVALIDMEDIATYPE : hr;
}

// Reads the RGB32 pixel at `point` (displayed coordinates) of one frame.
HRESULT readPixel(IMFSample* sample, const FrameLayout& layout, const QPoint& point, QRgb* color)
{
    const QPoint at = point + layout.origin;
    if (!QRect(QPoint(0, 0), layout.size).contains(at)) {
        return E_BOUNDS;
    }
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
        return hr;
    }
    const BYTE* pixel = nullptr;
    ComPtr<IMF2DBuffer> buffer2d;
    if (SUCCEEDED(buffer.As(&buffer2d))) {
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        hr = buffer2d->Lock2D(&scan0, &pitch);
        if (FAILED(hr)) {
            return hr;
        }
        if (qAbs(static_cast<qint64>(pitch)) < static_cast<qint64>(at.x() + 1) * kBytesPerPixel) {
            buffer2d->Unlock2D();
            return E_BOUNDS;
        }
        // scan0 is the displayed top row; pitch is negative for bottom-up.
        pixel = scan0 + static_cast<ptrdiff_t>(at.y()) * pitch + static_cast<ptrdiff_t>(at.x()) * kBytesPerPixel;
        *color = qRgb(pixel[2], pixel[1], pixel[0]); // BGRX
        return buffer2d->Unlock2D();
    }
    BYTE* data = nullptr;
    DWORD currentLength = 0;
    hr = buffer->Lock(&data, nullptr, &currentLength);
    if (FAILED(hr)) {
        return hr;
    }
    const qint64 absStride = qAbs(static_cast<qint64>(layout.stride));
    if (static_cast<qint64>(currentLength) < absStride * layout.size.height()) {
        buffer->Unlock();
        return MF_E_BUFFERTOOSMALL;
    }
    const qint64 row = layout.stride < 0 ? layout.size.height() - 1 - at.y() : at.y();
    pixel = data + row * absStride + static_cast<qint64>(at.x()) * kBytesPerPixel;
    *color = qRgb(pixel[2], pixel[1], pixel[0]); // BGRX
    return buffer->Unlock();
}

} // namespace

bool decodeAudioTrack(const QString& path, int sampleRate, DecodedAudio* out, QString* error)
{
    const auto failWith = [&](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    MediaFoundationScope mf;
    if (!mf.ok()) {
        return failWith(QStringLiteral("Cannot start Media Foundation"));
    }
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = openReader(path, false, reader);
    if (FAILED(hr)) {
        return failWith(hrMessage("Open audio reader", hr));
    }
    ComPtr<IMFMediaType> nativeType;
    if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &nativeType))) {
        return failWith(QStringLiteral("No audio track"));
    }
    hr = reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (SUCCEEDED(hr)) hr = setPcmOutput(reader.Get(), sampleRate);
    if (FAILED(hr)) {
        return failWith(hrMessage("Configure PCM output", hr));
    }
    ComPtr<IMFMediaType> pcmType;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pcmType);
    if (FAILED(hr)) {
        return failWith(hrMessage("Read PCM format", hr));
    }
    const UINT32 actualRate = MFGetAttributeUINT32(pcmType.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    const UINT32 channels = MFGetAttributeUINT32(pcmType.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
    const UINT32 bits = MFGetAttributeUINT32(pcmType.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0);
    const UINT32 blockAlign = MFGetAttributeUINT32(pcmType.Get(), MF_MT_AUDIO_BLOCK_ALIGNMENT, 0);
    if (actualRate == 0 || channels == 0 || bits != kDecodeBitDepth
        || blockAlign != channels * kDecodeBitDepth / kBitsPerByte) {
        return failWith(QStringLiteral("Unexpected PCM format: %1 Hz, %2 ch, %3 bit, align %4")
                            .arg(actualRate).arg(channels).arg(bits).arg(blockAlign));
    }

    DecodedAudio decoded;
    decoded.sampleRate = static_cast<int>(actualRate);
    bool haveFirst = false;
    for (;;) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            return failWith(hrMessage("Audio decode (ReadSample)", FAILED(hr) ? hr : E_FAIL));
        }
        if (sample.Get() != nullptr) {
            ComPtr<IMFMediaBuffer> buffer;
            hr = sample->ConvertToContiguousBuffer(&buffer);
            BYTE* data = nullptr;
            DWORD length = 0;
            if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, &length);
            if (FAILED(hr)) {
                return failWith(hrMessage("Lock decoded audio", hr));
            }
            const qint64 frames = static_cast<qint64>(length / blockAlign);
            const qint64 frameTime = std::llround(static_cast<double>(timestamp) * actualRate / kHnsPerSecond);
            if (!haveFirst && frames > 0) {
                decoded.firstFrame = frameTime;
                haveFirst = true;
            }
            const qint64 offset = frameTime - decoded.firstFrame;
            if (frames > 0 && (offset < 0 || offset + frames > kMaxDecodedFrames)) {
                buffer->Unlock();
                return failWith(QStringLiteral("Decoded audio timestamp out of range"));
            }
            if (frames > 0 && decoded.mono.size() < static_cast<size_t>(offset + frames)) {
                decoded.mono.resize(static_cast<size_t>(offset + frames), 0.0f);
            }
            const auto* pcm = reinterpret_cast<const qint16*>(data);
            for (qint64 i = 0; i < frames; ++i) {
                decoded.mono[static_cast<size_t>(offset + i)] = pcm[i * channels] / kInt16Scale;
            }
            hr = buffer->Unlock();
            if (FAILED(hr)) {
                return failWith(hrMessage("Unlock decoded audio", hr));
            }
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break;
        }
    }
    if (decoded.mono.empty()) {
        return failWith(QStringLiteral("Audio track decoded to no samples"));
    }
    *out = std::move(decoded);
    return true;
}

bool decodeVideoPixels(const QString& path, const QPoint& point, std::vector<DecodedVideoPixel>* out,
                       QString* error)
{
    const auto failWith = [&](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    MediaFoundationScope mf;
    if (!mf.ok()) {
        return failWith(QStringLiteral("Cannot start Media Foundation"));
    }
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = openReader(path, true, reader);
    if (FAILED(hr)) {
        return failWith(hrMessage("Open video reader", hr));
    }
    ComPtr<IMFMediaType> rgb;
    hr = reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&rgb);
    if (SUCCEEDED(hr)) hr = rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgb.Get());
    FrameLayout layout;
    if (SUCCEEDED(hr)) hr = queryFrameLayout(reader.Get(), &layout);
    if (FAILED(hr)) {
        return failWith(hrMessage("Configure RGB32 output", hr));
    }

    std::vector<DecodedVideoPixel> pixels;
    for (;;) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            return failWith(hrMessage("Video decode (ReadSample)", FAILED(hr) ? hr : E_FAIL));
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            hr = queryFrameLayout(reader.Get(), &layout);
            if (FAILED(hr)) {
                return failWith(hrMessage("Read changed video format", hr));
            }
        }
        if (sample.Get() != nullptr) {
            DecodedVideoPixel pixel;
            pixel.timeMs = static_cast<double>(timestamp) / kHnsPerMs;
            hr = readPixel(sample.Get(), layout, point, &pixel.color);
            if (FAILED(hr)) {
                return failWith(hrMessage("Read decoded pixel", hr));
            }
            pixels.push_back(pixel);
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break;
        }
    }
    if (pixels.empty()) {
        return failWith(QStringLiteral("Video track decoded to no frames"));
    }
    *out = std::move(pixels);
    return true;
}
