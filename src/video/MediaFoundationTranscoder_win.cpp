#include "video/IVideoTranscoder.h"

#include "VideoTranscoderFaultInjection.h"
#include "encoding/VideoBitrate.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QtGlobal>

// Qt first: nothing below uses std::min/std::max, so windows.h's macros are harmless.
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <atomic>
#include <climits>
#include <cstring>
#include <string>

// Offline MP4 trim + crop + H.264 re-encode with AAC passthrough, built on the
// synchronous Media Foundation Source Reader and Sink Writer. Media Foundation
// timestamps are in 100 ns units ("hns").
//
// Audio is copied as compressed AAC packets; there is no PCM re-encode
// fallback. If the Sink Writer cannot carry the source audio, transcode()
// fails and the source is retained (never a silent output).

using Microsoft::WRL::ComPtr;

namespace {

constexpr LONGLONG kHnsPerMs = 10000;
constexpr LONGLONG kHnsPerSecond = 10000000;
constexpr int kBytesPerPixel = 4;
constexpr int kPercentScale = 100;
constexpr int kMaxProgressBeforeFinish = 99;
constexpr int kCompletePercent = 100;
constexpr UINT32 kDefaultFrameRate = 30;
constexpr double kMinFrameRate = 1.0;
constexpr double kMaxFrameRate = 240.0;
// H.264 4:2:0 needs even dimensions; the backend always sends even crops,
// anything else is floored to even rather than handed to the encoder.
constexpr int kEvenAlignmentMask = ~1;
constexpr int kMinEvenSide = 2;
// AAC packs 1024 PCM frames per packet; used only when the source reader
// gives a packet no duration.
constexpr LONGLONG kAacFramesPerPacket = 1024;
// Audio coverage of the range is measured over every source packet that
// overlaps it, while the packet straddling the start is not copied and the
// one straddling the end is copied whole; kAudioCoverageToleranceMs
// (IVideoTranscoder.h) absorbs that difference.
// The finished file's duration must match the media the writer was given.
constexpr qint64 kOutputDurationToleranceMs = 100;
constexpr int kHresultHexDigits = 8;
constexpr int kHexBase = 16;

qint64 hnsToMs(LONGLONG hns)
{
    return hns >= 0 ? static_cast<qint64>((hns + kHnsPerMs / 2) / kHnsPerMs)
                    : -static_cast<qint64>((-hns + kHnsPerMs / 2) / kHnsPerMs);
}

QString hrText(const char* step, HRESULT hr)
{
    return QStringLiteral("%1 failed (0x%2)")
        .arg(QLatin1String(step))
        .arg(static_cast<quint32>(hr), kHresultHexDigits, kHexBase, QLatin1Char('0'));
}

QString cancelledMessage()
{
    return QStringLiteral("Cancelled");
}

// COM for the calling thread. RPC_E_CHANGED_MODE means the thread is already
// an STA; the synchronous Source Reader and Sink Writer work there too, we
// just must not uninitialize an apartment we did not enter.
class ComApartment
{
public:
    ComApartment() : m_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment()
    {
        if (SUCCEEDED(m_hr)) {
            CoUninitialize();
        }
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

    bool usable() const { return SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE; }
    HRESULT result() const { return m_hr; }

private:
    HRESULT m_hr;
};

// MFStartup/MFShutdown are reference counted, so nesting (probe() inside
// transcode(), or an encoder elsewhere in the process) is fine.
class MfSession
{
public:
    MfSession() : m_hr(MFStartup(MF_VERSION)) {}
    ~MfSession()
    {
        if (SUCCEEDED(m_hr)) {
            MFShutdown();
        }
    }
    MfSession(const MfSession&) = delete;
    MfSession& operator=(const MfSession&) = delete;

    HRESULT result() const { return m_hr; }

private:
    HRESULT m_hr;
};

std::wstring nativePath(const QString& path)
{
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
}

bool fileIdentity(const QString& path, BY_HANDLE_FILE_INFORMATION* info)
{
    const std::wstring native = nativePath(path);
    // FILE_FLAG_BACKUP_SEMANTICS allows opening without data access; symlinks
    // and junctions are followed, so the identity is that of the target.
    HANDLE handle = CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const BOOL ok = GetFileInformationByHandle(handle, info);
    CloseHandle(handle);
    return ok != FALSE;
}

// True when both paths name one file: the same spelling (NTFS is
// case-insensitive by default), or the same volume + file index, which covers
// symlinks, junctions, hard links and 8.3 short names.
bool isSameFile(const QString& a, const QString& b)
{
    const QString first = QDir::cleanPath(QFileInfo(a).absoluteFilePath());
    const QString second = QDir::cleanPath(QFileInfo(b).absoluteFilePath());
    if (first.compare(second, Qt::CaseInsensitive) == 0) {
        return true;
    }
    BY_HANDLE_FILE_INFORMATION firstInfo {};
    BY_HANDLE_FILE_INFORMATION secondInfo {};
    return fileIdentity(a, &firstInfo) && fileIdentity(b, &secondInfo)
        && firstInfo.dwVolumeSerialNumber == secondInfo.dwVolumeSerialNumber
        && firstInfo.nFileIndexHigh == secondInfo.nFileIndexHigh
        && firstInfo.nFileIndexLow == secondInfo.nFileIndexLow;
}

HRESULT openReader(const QString& path, bool decodeVideo, ComPtr<IMFSourceReader>& reader)
{
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 3);
    if (SUCCEEDED(hr) && decodeVideo) {
        // Lets the reader convert decoded YUV to RGB32.
        hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }
    // Software decode keeps decoded frames in system memory with a valid 2D
    // pitch, as MediaFoundationPlayer does.
    if (SUCCEEDED(hr)) {
        hr = attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
    }
    if (SUCCEEDED(hr)) {
        hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
    }
    const std::wstring native = nativePath(path);
    if (SUCCEEDED(hr)) {
        hr = MFCreateSourceReaderFromURL(native.c_str(), attributes.Get(), &reader);
    }
    return hr;
}

HRESULT presentationDuration(IMFSourceReader* reader, LONGLONG* durationHns)
{
    PROPVARIANT var;
    PropVariantInit(&var);
    HRESULT hr = reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var);
    if (SUCCEEDED(hr)) {
        if (var.vt == VT_UI8) {
            *durationHns = static_cast<LONGLONG>(var.uhVal.QuadPart);
        } else if (var.vt == VT_I8) {
            *durationHns = var.hVal.QuadPart;
        } else {
            hr = MF_E_ATTRIBUTENOTFOUND;
        }
    }
    PropVariantClear(&var);
    return hr;
}

// The displayed area of a video type in its own frame: the minimum display
// aperture when present (H.264 pads coded frames to 16 rows, e.g. 1080 ->
// 1088), otherwise the whole frame.
HRESULT displayArea(IMFMediaType* type, QRect* area)
{
    UINT32 width = 0;
    UINT32 height = 0;
    const HRESULT hr = MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) {
        return hr;
    }
    const QRect frame(0, 0, static_cast<int>(width), static_cast<int>(height));
    *area = frame;
    MFVideoArea aperture {};
    UINT32 blobSize = 0;
    if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&aperture),
                                sizeof(aperture), &blobSize))
        && blobSize == sizeof(aperture) && aperture.Area.cx > 0 && aperture.Area.cy > 0) {
        const QRect shown(aperture.OffsetX.value, aperture.OffsetY.value, static_cast<int>(aperture.Area.cx),
                          static_cast<int>(aperture.Area.cy));
        *area = shown.intersected(frame);
    }
    return area->isEmpty() ? MF_E_INVALIDMEDIATYPE : S_OK;
}

// Decoded RGB32 frame geometry as the reader currently delivers it. Re-read
// on MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED: the H.264 decoder usually
// reports its real output type with the first frame.
struct DecodedVideoLayout {
    QSize frameSize;
    QPoint origin;   // top-left of the displayed area inside the decoded frame
    LONG stride = 0; // signed, negative = bottom-up; only used without IMF2DBuffer
};

HRESULT queryDecodedLayout(IMFSourceReader* reader, DecodedVideoLayout* layout)
{
    ComPtr<IMFMediaType> type;
    HRESULT hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
    if (FAILED(hr)) {
        return hr;
    }
    GUID subtype = GUID_NULL;
    hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr)) {
        return hr;
    }
    if (subtype != MFVideoFormat_RGB32) {
        return MF_E_INVALIDMEDIATYPE;
    }
    UINT32 width = 0;
    UINT32 height = 0;
    hr = MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr)) {
        return hr;
    }
    QRect area;
    hr = displayArea(type.Get(), &area);
    if (FAILED(hr)) {
        return hr;
    }
    UINT32 rawStride = 0;
    LONG stride = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride))) {
        stride = static_cast<LONG>(static_cast<INT32>(rawStride));
    } else {
        hr = MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride);
        if (FAILED(hr)) {
            return hr;
        }
    }
    if (stride == 0) {
        return MF_E_INVALIDMEDIATYPE;
    }
    layout->frameSize = QSize(static_cast<int>(width), static_cast<int>(height));
    layout->origin = area.topLeft();
    layout->stride = stride;
    return S_OK;
}

// Locks a decoded frame and exposes its displayed top row and signed pitch.
class LockedVideoBuffer
{
public:
    ~LockedVideoBuffer() { unlock(); }

    HRESULT lock(IMFSample* sample, const DecodedVideoLayout& layout)
    {
        HRESULT hr = sample->ConvertToContiguousBuffer(&m_buffer);
        if (FAILED(hr)) {
            return hr;
        }
        if (SUCCEEDED(m_buffer.As(&m_buffer2d))) {
            // Lock2D returns the displayed top row; a negative pitch walks a
            // bottom-up buffer backwards.
            // The packed size must cover the frame the current type describes.
            DWORD contiguousLength = 0;
            hr = m_buffer2d->GetContiguousLength(&contiguousLength);
            if (FAILED(hr)) {
                return hr;
            }
            if (static_cast<qint64>(contiguousLength) < static_cast<qint64>(layout.frameSize.width())
                    * kBytesPerPixel * layout.frameSize.height()) {
                return MF_E_BUFFERTOOSMALL;
            }
            hr = m_buffer2d->Lock2D(&m_scan0, &m_pitch);
            if (FAILED(hr)) {
                return hr;
            }
            m_locked2d = true;
            return S_OK;
        }
        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        hr = m_buffer->Lock(&data, &maxLength, &currentLength);
        if (FAILED(hr)) {
            return hr;
        }
        m_locked = true;
        const qint64 absStride = qAbs(static_cast<qint64>(layout.stride));
        if (static_cast<qint64>(currentLength) < absStride * layout.frameSize.height()) {
            return MF_E_BUFFERTOOSMALL;
        }
        m_pitch = layout.stride;
        m_scan0 = layout.stride < 0 ? data + absStride * (layout.frameSize.height() - 1) : data;
        return S_OK;
    }

    HRESULT unlock()
    {
        HRESULT hr = S_OK;
        if (m_locked2d) {
            hr = m_buffer2d->Unlock2D();
            m_locked2d = false;
        } else if (m_locked) {
            hr = m_buffer->Unlock();
            m_locked = false;
        }
        return hr;
    }

    const BYTE* row(int y) const { return m_scan0 + static_cast<ptrdiff_t>(y) * m_pitch; }
    LONG pitch() const { return m_pitch; }

private:
    ComPtr<IMFMediaBuffer> m_buffer;
    ComPtr<IMF2DBuffer> m_buffer2d;
    BYTE* m_scan0 = nullptr;
    LONG m_pitch = 0;
    bool m_locked2d = false;
    bool m_locked = false;
};

// Copies `crop` (video pixels) out of a decoded RGB32 sample into a new
// bottom-up RGB32 buffer for the Sink Writer: an RGB32 input type without
// MF_MT_DEFAULT_STRIDE is bottom-up, the same layout MediaFoundationEncoder
// writes.
HRESULT copyCrop(IMFSample* sample, const DecodedVideoLayout& layout, const QRect& crop,
                 ComPtr<IMFMediaBuffer>& output)
{
    const QRect source = crop.translated(layout.origin);
    if (!QRect(QPoint(0, 0), layout.frameSize).contains(source)) {
        return E_BOUNDS;
    }
    LockedVideoBuffer input;
    HRESULT hr = input.lock(sample, layout);
    if (FAILED(hr)) {
        return hr;
    }
    const qint64 rowBytes = static_cast<qint64>(crop.width()) * kBytesPerPixel;
    if (qAbs(static_cast<qint64>(input.pitch())) < static_cast<qint64>(source.right() + 1) * kBytesPerPixel) {
        return E_BOUNDS;
    }
    const DWORD outputBytes = static_cast<DWORD>(rowBytes * crop.height());
    hr = MFCreateMemoryBuffer(outputBytes, &output);
    if (FAILED(hr)) {
        return hr;
    }
    BYTE* dst = nullptr;
    hr = output->Lock(&dst, nullptr, nullptr);
    if (FAILED(hr)) {
        return hr;
    }
    const ptrdiff_t xOffset = static_cast<ptrdiff_t>(source.x()) * kBytesPerPixel;
    for (int row = 0; row < crop.height(); ++row) {
        BYTE* dstRow = dst + static_cast<ptrdiff_t>(crop.height() - 1 - row) * rowBytes;
        std::memcpy(dstRow, input.row(source.y() + row) + xOffset, static_cast<size_t>(rowBytes));
    }
    hr = output->Unlock();
    if (SUCCEEDED(hr)) {
        hr = output->SetCurrentLength(outputBytes);
    }
    if (SUCCEEDED(hr)) {
        hr = input.unlock();
    }
    return hr;
}

HRESULT createVideoType(const GUID& subtype, const QSize& size, UINT32 fpsNum, UINT32 fpsDen,
                        ComPtr<IMFMediaType>& type)
{
    HRESULT hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, subtype);
    if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) {
        hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(size.width()),
                                static_cast<UINT32>(size.height()));
    }
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, fpsNum, fpsDen);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    return hr;
}

// Opens `path` for its first audio stream only, delivering the compressed
// packets as they are (nothing is decoded). `hasAudio` is false, with S_OK
// and a null reader, for a file without an audio stream.
HRESULT openAudioPacketReader(const QString& path, bool* hasAudio, ComPtr<IMFSourceReader>& reader)
{
    *hasAudio = false;
    HRESULT hr = openReader(path, false, reader);
    if (FAILED(hr)) {
        return hr;
    }
    ComPtr<IMFMediaType> nativeType;
    hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &nativeType);
    if (hr == MF_E_INVALIDSTREAMNUMBER) {
        reader.Reset();
        return S_OK;
    }
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    // The native type keeps the packets compressed: nothing is decoded.
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, nativeType.Get());
    if (SUCCEEDED(hr)) {
        *hasAudio = true;
    }
    return hr;
}

// Reads the next compressed packet. `sample` stays null for stream ticks and
// at the end of the stream, which sets `endOfStream`.
HRESULT readAudioPacket(IMFSourceReader* reader, ComPtr<IMFSample>& sample, LONGLONG* timestamp, bool* endOfStream)
{
    DWORD flags = 0;
    sample.Reset();
    const HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, timestamp, &sample);
    if (FAILED(hr)) {
        return hr;
    }
    if (flags & MF_SOURCE_READERF_ERROR) {
        return E_FAIL;
    }
    *endOfStream = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
    return S_OK;
}

// [first packet start, last packet end) of the first audio stream, read off
// the packets: MP4 exposes no per-track duration through the Source Reader.
// `hasAudio` is false (and S_OK returned) for a file without audio packets.
HRESULT measureAudioRange(const QString& path, bool* hasAudio, LONGLONG* firstHns, LONGLONG* endHns)
{
    *firstHns = 0;
    *endHns = 0;
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = openAudioPacketReader(path, hasAudio, reader);
    if (FAILED(hr) || !*hasAudio) {
        return hr;
    }
    bool haveFirst = false;
    for (;;) {
        ComPtr<IMFSample> sample;
        LONGLONG timestamp = 0;
        bool endOfStream = false;
        hr = readAudioPacket(reader.Get(), sample, &timestamp, &endOfStream);
        if (FAILED(hr)) {
            return hr;
        }
        if (sample.Get() != nullptr) {
            LONGLONG duration = 0;
            if (FAILED(sample->GetSampleDuration(&duration)) || duration < 0) {
                duration = 0;
            }
            if (!haveFirst || timestamp < *firstHns) {
                *firstHns = timestamp;
            }
            if (!haveFirst || timestamp + duration > *endHns) {
                *endHns = timestamp + duration;
            }
            haveFirst = true;
        }
        if (endOfStream) {
            break;
        }
    }
    *hasAudio = haveFirst;
    return S_OK;
}

// True when a data-carrying audio packet of `path` starts inside
// [startHns, endHns): exactly the packets handleAudio() copies (the packet
// straddling the start is not one of them). Seeks to the start, so only a
// few packets are read however long the file is.
HRESULT audioPacketStartsInRange(const QString& path, LONGLONG startHns, LONGLONG endHns, bool* found)
{
    *found = false;
    bool hasAudio = false;
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = openAudioPacketReader(path, &hasAudio, reader);
    if (FAILED(hr) || !hasAudio) {
        return hr;
    }
    if (startHns > 0) {
        PROPVARIANT position;
        PropVariantInit(&position);
        hr = InitPropVariantFromInt64(startHns, &position);
        // Lands on the packet at or before startHns; earlier ones are skipped below.
        if (SUCCEEDED(hr)) hr = reader->SetCurrentPosition(GUID_NULL, position);
        PropVariantClear(&position);
        if (FAILED(hr)) {
            return hr;
        }
    }
    for (;;) {
        ComPtr<IMFSample> sample;
        LONGLONG timestamp = 0;
        bool endOfStream = false;
        hr = readAudioPacket(reader.Get(), sample, &timestamp, &endOfStream);
        if (FAILED(hr)) {
            return hr;
        }
        if (sample.Get() != nullptr && timestamp >= startHns) {
            DWORD length = 0;
            const bool dataless = SUCCEEDED(sample->GetTotalLength(&length)) && length == 0;
            if (!dataless) {
                *found = timestamp < endHns;
                return S_OK;
            }
        }
        if (endOfStream) {
            return S_OK;
        }
    }
}

void removeOutput(const QString& path)
{
    if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "MediaFoundationTranscoder: cannot remove partial output" << path;
    }
}

struct WriteOutcome {
    QString error;
    bool cancelled = false;
    QSize outputSize;
    LONGLONG writtenEndHns = 0;        // end of the media handed to the writer
    LONGLONG originHns = 0;            // source time the output starts at
    qint64 sourceAudioCoverageMs = -1; // -1 = the source has no audio
};

class MediaFoundationTranscoder final : public IVideoTranscoder, public VideoTranscoderFaultInjection
{
public:
    VideoTranscodeResult transcode(const VideoTranscodeRequest& request, const ProgressCallback& progress) override;
    VideoFileProbe probe(const QString& filePath) override;
    void setFaultForTesting(VideoTranscodeFault fault) override { m_fault.store(fault); }

private:
    // Both run inside transcode()'s COM apartment and MF session. writeOutput()
    // owns every reader/writer object, so all of them are released (and the
    // output file handle closed) by the time it returns.
    bool writeOutput(const VideoTranscodeRequest& request, const ProgressCallback& progress,
                     VideoTranscodeFault fault, WriteOutcome* outcome);
    bool validateOutput(const QString& path, const WriteOutcome& written, bool* audioCopied, QString* error);

    std::atomic<VideoTranscodeFault> m_fault{VideoTranscodeFault::None};
};

VideoFileProbe MediaFoundationTranscoder::probe(const QString& filePath)
{
    VideoFileProbe result;
    if (!QFile::exists(filePath)) {
        return result;
    }
    ComApartment com;
    if (!com.usable()) {
        return result;
    }
    MfSession mf;
    if (FAILED(mf.result())) {
        return result;
    }
    ComPtr<IMFSourceReader> reader;
    if (FAILED(openReader(filePath, false, reader))) {
        return result;
    }
    ComPtr<IMFMediaType> videoType;
    if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &videoType))) {
        return result;
    }
    QRect area;
    LONGLONG durationHns = 0;
    if (FAILED(displayArea(videoType.Get(), &area)) || FAILED(presentationDuration(reader.Get(), &durationHns))) {
        return result;
    }
    ComPtr<IMFMediaType> audioType;
    result.videoSize = area.size();
    result.durationMs = hnsToMs(durationHns);
    result.hasAudio = SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &audioType));
    if (result.hasAudio) {
        bool hasPackets = false;
        LONGLONG firstHns = 0;
        LONGLONG endHns = 0;
        if (FAILED(measureAudioRange(filePath, &hasPackets, &firstHns, &endHns))) {
            return result; // Audio that cannot be inspected leaves the probe invalid.
        }
        if (hasPackets) {
            result.audioStartMs = hnsToMs(firstHns);
            result.audioEndMs = hnsToMs(endHns);
        }
    }
    result.valid = !result.videoSize.isEmpty() && result.durationMs > 0;
    return result;
}

VideoTranscodeResult MediaFoundationTranscoder::transcode(const VideoTranscodeRequest& request,
                                                          const ProgressCallback& progress)
{
    VideoTranscodeResult result;
    const auto reject = [&](const QString& message) {
        if (message == cancelledMessage()) {
            qDebug() << "MediaFoundationTranscoder:" << message;
        } else {
            qWarning() << "MediaFoundationTranscoder:" << message;
        }
        result.success = false;
        result.audioCopied = false;
        result.errorMessage = message;
        return result;
    };
    // Only called with no reader or writer alive, so the file is closed.
    const auto fail = [&](const QString& message) {
        removeOutput(request.outputPath);
        return reject(message);
    };

    if (request.outputPath.isEmpty()) {
        return reject(QStringLiteral("No output path"));
    }
    // Never remove the source: an output that aliases the input is refused
    // before anything touches the output path.
    if (isSameFile(request.outputPath, request.inputPath)) {
        return reject(QStringLiteral("Output path must differ from the input path"));
    }

    const VideoTranscodeFault fault = m_fault.load();
    WriteOutcome outcome;
    bool written = false;
    bool validated = false;
    bool audioCopied = false;
    QString validationError;
    {
        ComApartment com;
        if (!com.usable()) {
            return fail(hrText("CoInitializeEx", com.result()));
        }
        MfSession mf;
        if (FAILED(mf.result())) {
            return fail(hrText("MFStartup", mf.result()));
        }
        written = writeOutput(request, progress, fault, &outcome);
        if (written) {
            validated = validateOutput(request.outputPath, outcome, &audioCopied, &validationError);
        }
    }

    if (!written) {
        return fail(outcome.cancelled ? cancelledMessage() : outcome.error);
    }
    if (!validated) {
        return fail(validationError);
    }
    // Completion is reported only for a validated output. Returning false
    // here still cancels: the caller asked to stop, so nothing is kept.
    if (progress && !progress(kCompletePercent)) {
        return fail(cancelledMessage());
    }
    result.success = true;
    result.audioCopied = audioCopied;
    result.startMs = hnsToMs(outcome.originHns);
    return result;
}

bool MediaFoundationTranscoder::writeOutput(const VideoTranscodeRequest& request, const ProgressCallback& progress,
                                            VideoTranscodeFault fault, WriteOutcome* outcome)
{
    const auto failWith = [&](const QString& message) {
        outcome->error = message;
        return false;
    };
    const auto failAudio = [&](const char* what, const char* step, HRESULT hr) {
        return failWith(QStringLiteral("%1 (%2)").arg(QLatin1String(what), hrText(step, hr)));
    };

    if (!QFile::exists(request.inputPath)) {
        return failWith(QStringLiteral("Input file does not exist"));
    }
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = openReader(request.inputPath, true, reader);
    if (FAILED(hr)) {
        return failWith(hrText("Open input (MFCreateSourceReaderFromURL)", hr));
    }

    // ---- Source video: displayed size, frame rate, RGB32 decode ----
    ComPtr<IMFMediaType> nativeVideo;
    hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeVideo);
    if (hr == MF_E_INVALIDSTREAMNUMBER) {
        return failWith(QStringLiteral("Input has no video track"));
    }
    if (FAILED(hr)) {
        return failWith(hrText("GetNativeMediaType(input video)", hr));
    }
    QRect displayed;
    hr = displayArea(nativeVideo.Get(), &displayed);
    if (FAILED(hr)) {
        return failWith(hrText("Read input video size", hr));
    }
    UINT32 fpsNum = 0;
    UINT32 fpsDen = 0;
    if (FAILED(MFGetAttributeRatio(nativeVideo.Get(), MF_MT_FRAME_RATE, &fpsNum, &fpsDen)) || fpsNum == 0
        || fpsDen == 0 || static_cast<double>(fpsNum) / fpsDen < kMinFrameRate
        || static_cast<double>(fpsNum) / fpsDen > kMaxFrameRate) {
        fpsNum = kDefaultFrameRate;
        fpsDen = 1;
    }
    const int frameRate = qMax(1, static_cast<int>(fpsNum / fpsDen));
    const LONGLONG frameDurationHns = kHnsPerSecond * static_cast<LONGLONG>(fpsDen) / static_cast<LONGLONG>(fpsNum);

    // Crop coordinates are video pixels relative to the displayed area.
    const QRect frameRect(QPoint(0, 0), displayed.size());
    QRect crop = request.cropRect.isEmpty() ? frameRect : request.cropRect.intersected(frameRect);
    crop.setWidth(crop.width() & kEvenAlignmentMask);
    crop.setHeight(crop.height() & kEvenAlignmentMask);
    if (crop.width() < kMinEvenSide || crop.height() < kMinEvenSide) {
        return failWith(QStringLiteral("Crop rectangle is outside the video"));
    }
    outcome->outputSize = crop.size();

    hr = reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (FAILED(hr)) {
        return failWith(hrText("Select input video", hr));
    }
    ComPtr<IMFMediaType> rgbType;
    hr = MFCreateMediaType(&rgbType);
    if (SUCCEEDED(hr)) hr = rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgbType.Get());
    if (FAILED(hr)) {
        return failWith(hrText("Decode input video to RGB32", hr));
    }
    DecodedVideoLayout layout;
    hr = queryDecodedLayout(reader.Get(), &layout);
    if (FAILED(hr)) {
        return failWith(hrText("Read decoded video format", hr));
    }

    // ---- Time range ----
    LONGLONG totalHns = 0;
    hr = presentationDuration(reader.Get(), &totalHns);
    if (FAILED(hr) || totalHns <= 0) {
        return failWith(QStringLiteral("Input has no duration"));
    }
    const qint64 totalMs = hnsToMs(totalHns);
    const qint64 startMs = qBound<qint64>(0, request.startMs, totalMs);
    const qint64 endMs = request.endMs < 0 ? totalMs : qBound(startMs, request.endMs, totalMs);
    if (endMs <= startMs) {
        return failWith(QStringLiteral("Invalid time range"));
    }
    const LONGLONG startHns = static_cast<LONGLONG>(startMs) * kHnsPerMs;
    const LONGLONG endHns = static_cast<LONGLONG>(endMs) * kHnsPerMs;
    // The output timeline starts at originHns: the timestamp of the frame
    // shown at startHns, settled once the first frame after the start has
    // been read (or the video has ended). The MPEG-4 sink writes video at a
    // constant frame rate and drops the timing of a shortened first frame,
    // so a start inside a frame cannot be expressed; anchoring both streams
    // at that frame keeps audio and video aligned at the cost of starting up
    // to one frame early. Nothing is written before the origin is settled.
    LONGLONG originHns = startHns;
    bool originKnown = false;
    LONGLONG spanHns = endHns - startHns;

    // ---- Source audio: compressed AAC, delivered as-is ----
    ComPtr<IMFMediaType> nativeAudio;
    hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &nativeAudio);
    const bool hasAudio = SUCCEEDED(hr);
    if (!hasAudio && hr != MF_E_INVALIDSTREAMNUMBER) {
        return failAudio("Cannot inspect source audio", "GetNativeMediaType(audio)", hr);
    }
    // Only packets starting inside the range are copied (see handleAudio). A
    // range the source audio never reaches gets no audio stream at all and
    // exports video only, as the source is there: the Sink Writer cannot
    // finalize a stream that received no samples, and failing instead would
    // make the tail of a recording whose audio stopped early impossible to
    // export. Checked against the requested start: the output origin can lie
    // up to one frame earlier (originHns), so audio that ends inside that
    // sliver of an otherwise video-only range is not carried over.
    bool copyAudio = false;
    LONGLONG audioPacketHns = 0;
    if (hasAudio) {
        GUID audioSubtype = GUID_NULL;
        hr = nativeAudio->GetGUID(MF_MT_SUBTYPE, &audioSubtype);
        if (FAILED(hr)) {
            return failAudio("Cannot inspect source audio", "GetGUID(audio subtype)", hr);
        }
        if (audioSubtype != MFAudioFormat_AAC) {
            // Passthrough is only defined for AAC; never fall back to silence.
            return failWith(QStringLiteral("Cannot preserve source audio: the source audio is not AAC"));
        }
        const UINT32 sampleRate = MFGetAttributeUINT32(nativeAudio.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
        audioPacketHns = sampleRate > 0 ? kAacFramesPerPacket * kHnsPerSecond / static_cast<LONGLONG>(sampleRate) : 0;
        hr = audioPacketStartsInRange(request.inputPath, startHns, endHns, &copyAudio);
        if (FAILED(hr)) {
            return failAudio("Cannot inspect source audio", "Locate audio in the range", hr);
        }
    }
    if (copyAudio) {
        hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
        // Setting the native type keeps the packets compressed (no decoder).
        if (SUCCEEDED(hr)) {
            hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, nativeAudio.Get());
        }
        if (FAILED(hr)) {
            return failAudio("Cannot read source audio", "Select source audio", hr);
        }
    }

    // ---- Writer: MPEG-4 with H.264 video and (optionally) AAC passthrough ----
    QFile::remove(request.outputPath);
    ComPtr<IMFAttributes> writerAttributes;
    hr = MFCreateAttributes(&writerAttributes, 2);
    if (SUCCEEDED(hr)) hr = writerAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    // The container is explicit so the output extension never matters.
    if (SUCCEEDED(hr)) hr = writerAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    // Throttling stays enabled (the default): WriteSample blocks while the
    // encoder is behind, so decoded RGB32 frames cannot pile up in memory.
    // Source audio that ends well before the video leaves the audio stream
    // idle while video is still written; tst_VideoTranscoder::earlyEndingAudio
    // (run under a hang guard) verifies that case completes.
    ComPtr<IMFSinkWriter> writer;
    const std::wstring outputPath = nativePath(request.outputPath);
    if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(outputPath.c_str(), nullptr, writerAttributes.Get(), &writer);
    if (FAILED(hr)) {
        return failWith(hrText("Create output writer (MFCreateSinkWriterFromURL)", hr));
    }

    const int bitrate = request.videoBitrate > 0
        ? request.videoBitrate
        : SnapTray::VideoBitrate::forQuality(crop.size(), frameRate, kDefaultTranscodeQuality);
    DWORD videoStream = 0;
    ComPtr<IMFMediaType> h264Type;
    hr = createVideoType(MFVideoFormat_H264, crop.size(), fpsNum, fpsDen, h264Type);
    if (SUCCEEDED(hr)) hr = h264Type->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate));
    if (SUCCEEDED(hr)) hr = writer->AddStream(h264Type.Get(), &videoStream);
    ComPtr<IMFMediaType> encoderInputType;
    if (SUCCEEDED(hr)) hr = createVideoType(MFVideoFormat_RGB32, crop.size(), fpsNum, fpsDen, encoderInputType);
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(videoStream, encoderInputType.Get(), nullptr);
    if (FAILED(hr)) {
        return failWith(hrText("Configure H.264 output", hr));
    }

    DWORD audioStream = 0;
    if (copyAudio) {
        ComPtr<IMFMediaType> passthroughType;
        hr = MFCreateMediaType(&passthroughType);
        if (SUCCEEDED(hr)) hr = nativeAudio->CopyAllItems(passthroughType.Get());
        if (SUCCEEDED(hr) && fault == VideoTranscodeFault::AudioInputUnsupported) {
            hr = MF_E_INVALIDMEDIATYPE;
        }
        if (SUCCEEDED(hr)) hr = writer->AddStream(passthroughType.Get(), &audioStream);
        // Input type == target type: the Sink Writer inserts no encoder and
        // muxes the source packets as they are.
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audioStream, passthroughType.Get(), nullptr);
        if (FAILED(hr)) {
            // Never fall back to a silent output.
            return failAudio("Cannot preserve source audio", "Add AAC passthrough stream", hr);
        }
    }

    hr = writer->BeginWriting();
    if (FAILED(hr)) {
        return failWith(hrText("BeginWriting", hr));
    }

    if (startHns > 0) {
        PROPVARIANT position;
        PropVariantInit(&position);
        hr = InitPropVariantFromInt64(startHns, &position);
        // Lands on the key frame at or before startHns; earlier frames and
        // packets are decoded/read and dropped below.
        if (SUCCEEDED(hr)) hr = reader->SetCurrentPosition(GUID_NULL, position);
        PropVariantClear(&position);
        if (FAILED(hr)) {
            return failWith(hrText("Seek input (SetCurrentPosition)", hr));
        }
    }

    // ---- Video: the frame on screen at the range start is shown from 0 ----
    // The newest decoded frame at or before the range start is the one on
    // screen there; it is the only pre-start frame written (shown from 0).
    // The sample is kept and cropped when written, instead of cropping every
    // frame from the seek key frame to the start.
    struct LeadFrame {
        ComPtr<IMFSample> sample;
        DecodedVideoLayout layout; // the decoded geometry when the sample was read
        LONGLONG durationHns = 0;
        LONGLONG timestampHns = 0;
    } lead;
    bool haveLead = false;
    const auto settleOrigin = [&]() {
        if (originKnown) {
            return;
        }
        originKnown = true;
        if (haveLead) {
            originHns = lead.timestampHns;
        }
        spanHns = endHns - originHns;
    };
    bool haveVideoOut = false;
    LONGLONG lastVideoOutHns = 0;
    LONGLONG videoEndOutHns = 0;
    int lastPercent = -1;

    const auto writeVideo = [&](IMFMediaBuffer* buffer, LONGLONG outTime, LONGLONG duration) {
        if (haveVideoOut && outTime <= lastVideoOutHns) {
            return true; // duplicate timestamp: keep the frame already written
        }
        ComPtr<IMFSample> sample;
        HRESULT writeHr = MFCreateSample(&sample);
        if (SUCCEEDED(writeHr)) writeHr = sample->AddBuffer(buffer);
        if (SUCCEEDED(writeHr)) writeHr = sample->SetSampleTime(outTime);
        if (SUCCEEDED(writeHr)) writeHr = sample->SetSampleDuration(duration);
        if (SUCCEEDED(writeHr)) writeHr = writer->WriteSample(videoStream, sample.Get());
        if (FAILED(writeHr)) {
            return failWith(hrText("Encode video frame (WriteSample)", writeHr));
        }
        haveVideoOut = true;
        lastVideoOutHns = outTime;
        videoEndOutHns = qMax(videoEndOutHns, outTime + duration);
        const int percent = qBound(0, static_cast<int>(outTime * kPercentScale / spanHns), kMaxProgressBeforeFinish);
        if (percent != lastPercent) {
            lastPercent = percent;
            if (progress && !progress(percent)) {
                outcome->cancelled = true;
                return false;
            }
        }
        return true;
    };

    const auto writeLead = [&](LONGLONG durationHns) {
        ComPtr<IMFMediaBuffer> cropped;
        const HRESULT cropHr = copyCrop(lead.sample.Get(), lead.layout, crop, cropped);
        lead.sample.Reset();
        if (FAILED(cropHr)) {
            return failWith(hrText("Crop video frame", cropHr));
        }
        return writeVideo(cropped.Get(), 0, durationHns);
    };

    const auto handleVideo = [&](IMFSample* sample, LONGLONG timestamp) {
        LONGLONG duration = 0;
        if (FAILED(sample->GetSampleDuration(&duration)) || duration <= 0) {
            duration = frameDurationHns;
        }
        if (timestamp <= startHns) {
            // Frames from the seek key frame up to the start: keep the newest.
            lead.sample = sample;
            lead.layout = layout;
            lead.durationHns = duration;
            lead.timestampHns = timestamp;
            haveLead = true;
            return true;
        }
        settleOrigin();
        if (haveLead) {
            haveLead = false;
            if (!writeLead(timestamp - originHns)) {
                return false;
            }
        }
        ComPtr<IMFMediaBuffer> cropped;
        const HRESULT cropHr = copyCrop(sample, layout, crop, cropped);
        if (FAILED(cropHr)) {
            return failWith(hrText("Crop video frame", cropHr));
        }
        // Clamp so the last frame ends at the range end.
        return writeVideo(cropped.Get(), timestamp - originHns, qMin(duration, endHns - timestamp));
    };

    // ---- Audio: compressed packets copied with retimed timestamps ----
    // Media Foundation delivers one AAC packet (1024 frames) per sample.
    // Decision for the packet that straddles the output origin: it is NOT
    // copied. Its timestamp would be negative on the output timeline, and
    // retiming it to 0 would shift every following packet by up to one packet.
    // Dropping it costs < one packet of lead-in silence (<= 21.3 ms at 48 kHz)
    // and keeps every copied packet at its exact source offset. Unlike the
    // multi-packet buffers of AVFoundation, nothing past the start is lost.
    // The packet straddling the end is copied whole.
    int writtenAudioSamples = 0;
    bool haveAudioOut = false;
    LONGLONG lastAudioOutHns = 0;
    LONGLONG audioEndOutHns = 0;
    bool haveSourceAudio = false;
    LONGLONG sourceAudioStartHns = 0;
    LONGLONG sourceAudioEndHns = 0;

    const auto handleAudio = [&](IMFSample* sample, LONGLONG timestamp) {
        DWORD length = 0;
        HRESULT audioHr = sample->GetTotalLength(&length);
        if (FAILED(audioHr)) {
            return failAudio("Cannot read source audio", "GetTotalLength", audioHr);
        }
        if (length == 0) {
            return true; // data-less marker; means nothing to a passthrough writer
        }
        LONGLONG duration = 0;
        if (FAILED(sample->GetSampleDuration(&duration)) || duration <= 0) {
            duration = audioPacketHns;
        }
        const LONGLONG sampleEnd = timestamp + duration;
        // Source coverage of the interval counts every overlapping packet,
        // including the start-straddling one that is not copied.
        if (sampleEnd > originHns) {
            const LONGLONG coverStart = qMax(timestamp, originHns);
            const LONGLONG coverEnd = qMin(sampleEnd, endHns);
            sourceAudioStartHns = haveSourceAudio ? qMin(sourceAudioStartHns, coverStart) : coverStart;
            sourceAudioEndHns = haveSourceAudio ? qMax(sourceAudioEndHns, coverEnd) : coverEnd;
            haveSourceAudio = true;
        }
        if (timestamp < originHns) {
            return true; // before the origin, or straddling it (see above)
        }
        const bool faultHere = writtenAudioSamples++ == kVideoTranscodeFaultAudioSampleIndex;
        const LONGLONG outTime = timestamp - originHns;
        if (haveAudioOut && outTime <= lastAudioOutHns) {
            return failWith(QStringLiteral("Cannot retime source audio: timestamps are not increasing"));
        }
        audioHr = (faultHere && fault == VideoTranscodeFault::AudioRetimeFailure) ? E_FAIL
                                                                                  : sample->SetSampleTime(outTime);
        if (FAILED(audioHr)) {
            return failAudio("Cannot retime source audio", "SetSampleTime", audioHr);
        }
        audioHr = (faultHere && fault == VideoTranscodeFault::AudioAppendFailure)
            ? E_FAIL
            : writer->WriteSample(audioStream, sample);
        if (FAILED(audioHr)) {
            return failAudio("Cannot write source audio", "WriteSample(audio)", audioHr);
        }
        haveAudioOut = true;
        lastAudioOutHns = outTime;
        audioEndOutHns = qMax(audioEndOutHns, outTime + duration);
        return true;
    };

    // ---- Read loop: always pull the stream that is further behind, so the
    // MPEG-4 sink receives interleaved media ----
    bool videoDone = false;
    bool audioDone = !copyAudio;
    LONGLONG videoClock = LLONG_MIN;
    LONGLONG audioClock = LLONG_MIN;
    while (!videoDone || !audioDone) {
        const bool readVideo = !videoDone && (!originKnown || audioDone || videoClock <= audioClock);
        const DWORD stream = readVideo ? static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM)
                                       : static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(stream, 0, nullptr, &flags, &timestamp, &sample);
        if (SUCCEEDED(hr) && (flags & MF_SOURCE_READERF_ERROR)) {
            hr = E_FAIL;
        }
        if (FAILED(hr)) {
            return readVideo ? failWith(hrText("Read source video (ReadSample)", hr))
                             : failAudio("Cannot read source audio", "ReadSample", hr);
        }
        const DWORD typeChanged = MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED;
        if (flags & typeChanged) {
            if (!readVideo) {
                return failWith(QStringLiteral("Cannot preserve source audio: the audio format changed"));
            }
            hr = queryDecodedLayout(reader.Get(), &layout);
            if (FAILED(hr)) {
                return failWith(hrText("Read changed video format", hr));
            }
        }
        const bool hasTime = sample.Get() != nullptr || (flags & MF_SOURCE_READERF_STREAMTICK);
        if (hasTime) {
            (readVideo ? videoClock : audioClock) = timestamp;
        }
        if (sample.Get() != nullptr && timestamp < endHns) {
            const bool handled = readVideo ? handleVideo(sample.Get(), timestamp) : handleAudio(sample.Get(), timestamp);
            if (!handled) {
                return false;
            }
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) || (hasTime && timestamp >= endHns)) {
            (readVideo ? videoDone : audioDone) = true;
            if (readVideo) {
                settleOrigin(); // in case no frame after the start was read: the lead covers it all
            }
            // Deselect so the reader stops queueing this stream's samples.
            hr = reader->SetStreamSelection(stream, FALSE);
            if (FAILED(hr)) {
                return failWith(hrText("Deselect finished stream", hr));
            }
        }
    }

    if (haveLead) {
        // No frame after the start inside the range: the lead covers it all.
        haveLead = false;
        if (!writeLead(qMin(lead.durationHns, spanHns))) {
            return false;
        }
    }
    if (!haveVideoOut) {
        return failWith(QStringLiteral("No video frames in the selected range"));
    }
    if (copyAudio && !haveAudioOut) {
        // audioPacketStartsInRange() promised a packet, so this is a reader
        // inconsistency. The Sink Writer cannot finalize an audio stream
        // without samples, and the stream cannot be removed after BeginWriting.
        return failWith(QStringLiteral("Cannot preserve source audio: no audio packets in the selected range"));
    }

    hr = writer->Finalize();
    if (FAILED(hr)) {
        return failWith(hrText("Finish output (Finalize)", hr));
    }
    writer.Reset();
    reader.Reset();

    outcome->writtenEndHns = qMax(videoEndOutHns, audioEndOutHns);
    outcome->originHns = originHns;
    outcome->sourceAudioCoverageMs =
        hasAudio ? (haveSourceAudio ? hnsToMs(qMax<LONGLONG>(0, sourceAudioEndHns - sourceAudioStartHns)) : 0) : -1;
    return true;
}

// Validates the finished file before success is reported; the caller deletes
// the source only on success.
bool MediaFoundationTranscoder::validateOutput(const QString& path, const WriteOutcome& written, bool* audioCopied,
                                               QString* error)
{
    const VideoFileProbe outputProbe = probe(path);
    if (!outputProbe.valid || outputProbe.videoSize != written.outputSize) {
        *error = QStringLiteral("Output validation failed; source retained");
        return false;
    }
    const qint64 writtenMs = hnsToMs(written.writtenEndHns);
    if (qAbs(outputProbe.durationMs - writtenMs) > kOutputDurationToleranceMs) {
        *error = QStringLiteral("Output duration %1 ms does not match the %2 ms written; source retained")
                     .arg(outputProbe.durationMs)
                     .arg(writtenMs);
        return false;
    }
    // probe() already read the output's audio packets (an uninspectable
    // output is not valid, caught above).
    const qint64 outputAudioMs =
        outputProbe.audioEndMs >= 0 ? outputProbe.audioEndMs - outputProbe.audioStartMs : -1;
    // A selection the source audio does not reach (coverage within the
    // tolerance) has nothing to preserve; any other gap is a silent downgrade.
    if (written.sourceAudioCoverageMs > kAudioCoverageToleranceMs
        && (outputAudioMs <= 0 || outputAudioMs < written.sourceAudioCoverageMs - kAudioCoverageToleranceMs)) {
        *error = QStringLiteral("Output is missing source audio; source retained");
        return false;
    }
    *audioCopied = outputAudioMs > 0;
    return true;
}

} // namespace

std::unique_ptr<IVideoTranscoder> createMediaFoundationTranscoder()
{
    return std::make_unique<MediaFoundationTranscoder>();
}
