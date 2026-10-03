// Sequential Media Foundation decoder behind IVideoFrameReader. Mirrors the
// macOS AVFoundationFrameReader contract: ascending requests only, empty
// lead-in uses the first frame, gaps hold the last frame. Decodes to RGB32 on
// the CPU (no DXVA) so frames can be copied straight into QImage.
#include "video/IVideoFrameReader.h"
#include "video/MediaFoundationFrameCopy_win.h"

#include <QDebug>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <memory>

using Microsoft::WRL::ComPtr;

namespace {

constexpr LONGLONG kHnsPerMs = 10000;
constexpr double kDefaultFrameRate = 30.0;

qint64 hnsToMs(LONGLONG hns)
{
    return qint64((hns + kHnsPerMs / 2) / kHnsPerMs);
}

class MediaFoundationFrameReader final : public IVideoFrameReader
{
public:
    MediaFoundationFrameReader()
    {
        m_comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_mfResult = MFStartup(MF_VERSION);
    }

    ~MediaFoundationFrameReader() override
    {
        m_reader.Reset();
        if (SUCCEEDED(m_mfResult)) MFShutdown();
        if (SUCCEEDED(m_comResult) || m_comResult == RPC_E_CHANGED_MODE) {
            if (SUCCEEDED(m_comResult)) CoUninitialize();
        }
    }

    bool load(const QString& filePath) override
    {
        m_lastError.clear();
        if (FAILED(m_mfResult)) {
            m_lastError = QStringLiteral("Media Foundation unavailable");
            return false;
        }
        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 3);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
        if (SUCCEEDED(hr)) {
            hr = MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(filePath.utf16()), attributes.Get(), &m_reader);
        }
        if (FAILED(hr)) return fail(QStringLiteral("Failed to open %1").arg(filePath), hr);

        hr = m_reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (SUCCEEDED(hr)) hr = m_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (FAILED(hr)) return fail(QStringLiteral("No video stream"), hr);

        ComPtr<IMFMediaType> nativeType;
        hr = m_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType);
        if (FAILED(hr)) return fail(QStringLiteral("No native video type"), hr);
        UINT32 num = 0;
        UINT32 den = 0;
        if (SUCCEEDED(MFGetAttributeRatio(nativeType.Get(), MF_MT_FRAME_RATE, &num, &den)) && den != 0) {
            m_frameRate = double(num) / double(den);
        } else {
            m_frameRate = kDefaultFrameRate;
        }

        ComPtr<IMFMediaType> rgbType;
        hr = MFCreateMediaType(&rgbType);
        if (SUCCEEDED(hr)) hr = rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = m_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgbType.Get());
        if (FAILED(hr)) return fail(QStringLiteral("RGB32 output not supported"), hr);
        if (!readLayout()) return false;

        PROPVARIANT duration;
        PropVariantInit(&duration);
        hr = m_reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration);
        m_durationMs = SUCCEEDED(hr) && duration.vt == VT_UI8 ? hnsToMs(LONGLONG(duration.uhVal.QuadPart)) : 0;
        PropVariantClear(&duration);

        m_current = QImage();
        m_lookAhead = QImage();
        m_lookAheadMs = -1;
        m_lastRequestMs = -1;
        m_endOfStream = false;
        return true;
    }

    QImage frameAt(qint64 positionMs) override
    {
        if (!m_reader) {
            m_lastError = QStringLiteral("No video loaded");
            return {};
        }
        if (positionMs < m_lastRequestMs) {
            m_lastError = QStringLiteral("Video reader requires ascending timestamps");
            return {};
        }
        m_lastRequestMs = positionMs;
        // Advance until the look-ahead frame starts after the requested time;
        // the frame before it is the one displayed at positionMs.
        while (!m_endOfStream && (m_lookAhead.isNull() || m_lookAheadMs <= positionMs)) {
            if (!m_lookAhead.isNull()) {
                m_current = m_lookAhead;
                m_lookAhead = QImage();
            }
            if (!readNext()) return {};
        }
        if (m_current.isNull()) {
            // Lead-in before the first sample: use the first decoded frame.
            if (m_lookAhead.isNull()) {
                m_lastError = m_lastError.isEmpty() ? QStringLiteral("No video frames") : m_lastError;
                return {};
            }
            return m_lookAhead;
        }
        return m_current;
    }

    QSize videoSize() const override { return m_size; }
    qint64 duration() const override { return m_durationMs; }
    double frameRate() const override { return m_frameRate; }
    QString lastError() const override { return m_lastError; }

private:
    bool fail(const QString& message, HRESULT hr)
    {
        m_lastError = QStringLiteral("%1 (hr=0x%2)").arg(message).arg(ulong(hr), 8, 16, QChar('0'));
        qWarning() << "MediaFoundationFrameReader:" << m_lastError;
        m_reader.Reset();
        return false;
    }

    bool readLayout()
    {
        ComPtr<IMFMediaType> type;
        HRESULT hr = m_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
        if (FAILED(hr)) return fail(QStringLiteral("No current media type"), hr);
        UINT32 width = 0;
        UINT32 height = 0;
        hr = MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr) || width == 0 || height == 0) return fail(QStringLiteral("Invalid frame size"), hr);
        // Honour the display aperture when the decoder pads to macroblocks.
        MFVideoArea area{};
        UINT32 blobSize = 0;
        m_origin = QPoint(0, 0);
        m_size = QSize(int(width), int(height));
        if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &blobSize))
            && blobSize == sizeof(area) && area.Area.cx > 0 && area.Area.cy > 0) {
            m_origin = QPoint(area.OffsetX.value, area.OffsetY.value);
            m_size = QSize(area.Area.cx, area.Area.cy);
        }
        UINT32 stride = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
            m_defaultStride = LONG(stride);
        } else {
            m_defaultStride = LONG(width) * 4;
        }
        return true;
    }

    // Decodes one sample into m_lookAhead. Returns false on error.
    bool readNext()
    {
        for (;;) {
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            const HRESULT hr = m_reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
            if (FAILED(hr)) return fail(QStringLiteral("ReadSample failed"), hr);
            if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                if (!readLayout()) return false;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                m_endOfStream = true;
                return true;
            }
            if (!sample) continue; // stream tick or gap: keep reading
            QImage frame = copySample(sample.Get());
            if (frame.isNull()) return false;
            m_lookAhead = frame;
            m_lookAheadMs = hnsToMs(timestamp);
            return true;
        }
    }

    QImage copySample(IMFSample* sample)
    {
        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) {
            fail(QStringLiteral("No sample buffer"), hr);
            return {};
        }
        ComPtr<IMF2DBuffer> buffer2d;
        BYTE* scanline0 = nullptr;
        LONG pitch = 0;
        QImage frame;
        if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scanline0, &pitch))) {
            const uchar* origin = scanline0 + qsizetype(m_origin.y()) * pitch + qsizetype(m_origin.x()) * 4;
            frame = copyMediaFoundationRgb32Frame(origin, pitch, m_size);
            buffer2d->Unlock2D();
        } else {
            BYTE* data = nullptr;
            DWORD length = 0;
            hr = buffer->Lock(&data, nullptr, &length);
            if (FAILED(hr)) {
                fail(QStringLiteral("Buffer lock failed"), hr);
                return {};
            }
            // Positive default stride = top-down, negative = bottom-up (RGB32 is bottom-up by
            // default). Same arithmetic as MediaFoundationPlayer_win: the displayed top row of a
            // bottom-up buffer is the last allocated row, and rows advance backwards.
            const qsizetype rowPitch = qsizetype(m_defaultStride >= 0 ? m_defaultStride : -m_defaultStride);
            if (rowPitch <= 0 || qsizetype(length) < rowPitch * (m_origin.y() + m_size.height())) {
                buffer->Unlock();
                fail(QStringLiteral("Unexpected sample buffer size"), E_UNEXPECTED);
                return {};
            }
            const qsizetype allocatedRows = qsizetype(length) / rowPitch;
            const bool bottomUp = m_defaultStride < 0;
            const uchar* firstRow = bottomUp ? data + (allocatedRows - 1) * rowPitch : data;
            const qsizetype pitch = bottomUp ? -rowPitch : rowPitch;
            const uchar* origin = firstRow + qsizetype(m_origin.y()) * pitch + qsizetype(m_origin.x()) * 4;
            frame = copyMediaFoundationRgb32Frame(origin, pitch, m_size);
            buffer->Unlock();
        }
        return frame;
    }

    HRESULT m_comResult = E_FAIL;
    HRESULT m_mfResult = E_FAIL;
    ComPtr<IMFSourceReader> m_reader;
    QSize m_size;
    QPoint m_origin;
    LONG m_defaultStride = 0;
    double m_frameRate = kDefaultFrameRate;
    qint64 m_durationMs = 0;
    QImage m_current;
    QImage m_lookAhead;
    qint64 m_lookAheadMs = -1;
    qint64 m_lastRequestMs = -1;
    bool m_endOfStream = false;
    QString m_lastError;
};

} // namespace

std::unique_ptr<IVideoFrameReader> createMediaFoundationFrameReader()
{
    return std::make_unique<MediaFoundationFrameReader>();
}
