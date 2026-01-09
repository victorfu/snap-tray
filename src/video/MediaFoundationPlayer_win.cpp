#include "video/IVideoPlayer.h"

#ifdef Q_OS_WIN

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <Mfobjects.h>
#include <propvarutil.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "propsys.lib")

namespace {
constexpr int kBytesPerPixel = 4;
constexpr int kStrideAlignments[] = { 16, 32, 64, 128, 256 };
constexpr int kHeightAlignments[] = { 1, 2, 4, 8, 16, 32 };

struct StrideResult {
    int stride = 0;
    int usedHeight = 0;
    bool exact = false;
};

int alignUp(int value, int alignment)
{
    return ((value + alignment - 1) / alignment) * alignment;
}

StrideResult computeStrideFromLength(int width, int height, DWORD length, int defaultStride)
{
    StrideResult result{0, height, false};
    if (width <= 0 || height <= 0) {
        return result;
    }

    int minStride = width * kBytesPerPixel;
    int baseStride = defaultStride >= minStride ? defaultStride : minStride;

    for (int alignment : kHeightAlignments) {
        int alignedHeight = alignUp(height, alignment);
        if (alignedHeight <= 0) {
            continue;
        }
        if (length % static_cast<DWORD>(alignedHeight) != 0) {
            continue;
        }
        int stride = static_cast<int>(length / static_cast<DWORD>(alignedHeight));
        if (stride < minStride || (stride % kBytesPerPixel) != 0) {
            continue;
        }
        result.stride = stride;
        result.usedHeight = alignedHeight;
        result.exact = true;
        return result;
    }

    if (length < static_cast<DWORD>(baseStride * height)) {
        return result;
    }

    int maxStride = static_cast<int>(length / static_cast<DWORD>(height));
    if (maxStride <= baseStride) {
        result.stride = baseStride;
        return result;
    }

    int extraPerRow = maxStride - baseStride;
    if (extraPerRow < kBytesPerPixel) {
        result.stride = baseStride;
        return result;
    }

    for (int alignment : kStrideAlignments) {
        int candidate = alignUp(baseStride, alignment);
        if (candidate > baseStride && candidate <= maxStride) {
            result.stride = candidate;
            return result;
        }
    }

    int alignedDown = maxStride - (maxStride % kBytesPerPixel);
    result.stride = alignedDown >= minStride ? alignedDown : baseStride;
    return result;
}

}

// Forward declarations
class MediaFoundationPlayer;
class FrameReaderThread;

// Worker thread for reading video frames without blocking the UI
class FrameReaderThread : public QThread
{
    Q_OBJECT

public:
    explicit FrameReaderThread(QObject *parent = nullptr)
        : QThread(parent)
    {}

    void setReader(IMFSourceReader *reader) { m_reader = reader; }
    void setVideoSize(QSize size) { m_videoSize = size; }
    void setStride(int stride) { m_stride = stride; }
    void setFrameInterval(int intervalMs) { m_frameIntervalMs = intervalMs; }
    void setPlaybackRate(float rate) { m_playbackRate = rate; }

    void requestStop() { m_stopRequested = true; }
    void requestPause() { m_paused = true; }
    void requestResume() { m_paused = false; }
    void requestSeek(qint64 positionMs) {
        m_seekPosition = positionMs;
        m_seekRequested = true;
    }

signals:
    void frameReady(const QImage &frame, qint64 timestampMs);
    void endOfStream();
    void errorOccurred(const QString &message);

protected:
    void run() override
    {
        // Initialize COM for this thread
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            emit errorOccurred("Failed to initialize COM in reader thread");
            return;
        }

        while (!m_stopRequested) {
            // Handle pause
            if (m_paused) {
                QThread::msleep(10);
                continue;
            }

            if (!m_reader) {
                QThread::msleep(10);
                continue;
            }

            // Handle seek request
            if (m_seekRequested) {
                m_seekRequested = false;
                PROPVARIANT var;
                PropVariantInit(&var);
                var.vt = VT_I8;
                var.hVal.QuadPart = m_seekPosition * 10000;
                m_reader->SetCurrentPosition(GUID_NULL, var);
                PropVariantClear(&var);
            }

            // Read next frame
            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            IMFSample *sample = nullptr;

            hr = m_reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0,
                &streamIndex,
                &flags,
                &timestamp,
                &sample);

            if (FAILED(hr)) {
                emit errorOccurred(QString("ReadSample failed: 0x%1").arg(hr, 8, 16, QChar('0')));
                break;
            }

            // Check for end of stream
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                emit endOfStream();
                break;
            }

            if (sample) {
                QImage frame = extractFrame(sample);
                if (!frame.isNull()) {
                    qint64 timestampMs = timestamp / 10000;
                    emit frameReady(frame, timestampMs);
                }
                sample->Release();

                // Pace the frame reading based on playback rate
                int sleepMs = static_cast<int>(m_frameIntervalMs / m_playbackRate);
                if (sleepMs > 0) {
                    QThread::msleep(sleepMs);
                }
            }
        }

        CoUninitialize();
    }

private:
    QImage extractFrame(IMFSample *sample)
    {
        IMFMediaBuffer *buffer = nullptr;
        HRESULT hr = sample->GetBufferByIndex(0, &buffer);
        if (FAILED(hr)) {
            return QImage();
        }

        QImage frame;

        // Try IMF2DBuffer first to get actual stride (pitch) from the buffer
        IMF2DBuffer *buffer2D = nullptr;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&buffer2D));
        if (SUCCEEDED(hr)) {
            BYTE *data = nullptr;
            LONG pitch = 0;
            hr = buffer2D->Lock2D(&data, &pitch);
            if (SUCCEEDED(hr)) {
                // Debug log on first frame
                static bool firstFrameLogged = false;
                if (!firstFrameLogged) {
                    qDebug() << "MediaFoundationPlayer extractFrame (2D): videoSize:" << m_videoSize
                             << "pitch:" << pitch << "width*4:" << (m_videoSize.width() * 4);
                    firstFrameLogged = true;
                }

                if (m_stride != static_cast<int>(pitch)) {
                    m_stride = static_cast<int>(pitch);
                }

                frame = QImage(m_videoSize.width(), m_videoSize.height(), QImage::Format_RGB32);
                if (!frame.isNull()) {
                    int absPitch = qAbs(pitch);

                    if (pitch < 0) {
                        // Bottom-up buffer - flip vertically
                        for (int y = 0; y < m_videoSize.height(); y++) {
                            const BYTE *srcRow = data + (m_videoSize.height() - 1 - y) * absPitch;
                            memcpy(frame.scanLine(y), srcRow, m_videoSize.width() * 4);
                        }
                    } else {
                        // Top-down - copy directly
                        for (int y = 0; y < m_videoSize.height(); y++) {
                            const BYTE *srcRow = data + y * absPitch;
                            memcpy(frame.scanLine(y), srcRow, m_videoSize.width() * 4);
                        }
                    }
                }
                buffer2D->Unlock2D();
            }
            buffer2D->Release();
        } else {
            // Fallback: use regular buffer with m_stride
            static bool fallbackLogged = false;
            if (!fallbackLogged) {
                qDebug() << "MediaFoundationPlayer: IMF2DBuffer not available, using fallback. hr:" << Qt::hex << hr;
                fallbackLogged = true;
            }

            BYTE *data = nullptr;
            DWORD maxLength = 0;
            DWORD currentLength = 0;

            hr = buffer->Lock(&data, &maxLength, &currentLength);
            if (SUCCEEDED(hr)) {
                int height = m_videoSize.height();
                int width = m_videoSize.width();
                int minStride = width * kBytesPerPixel;
                int defaultStride = qAbs(m_stride);
                StrideResult fromMax = computeStrideFromLength(width, height, maxLength, defaultStride);
                StrideResult fromCurrent = computeStrideFromLength(width, height, currentLength, defaultStride);
                StrideResult chosen = fromCurrent;
                if (chosen.stride <= 0 || (!chosen.exact && fromMax.exact)) {
                    chosen = fromMax;
                }
                int strideToUse = chosen.stride;
                int strideSign = (m_stride < 0) ? -1 : 1;
                int signedStride = strideSign * strideToUse;
                int expectedSize = height * strideToUse;

                static bool fallbackFrameLogged = false;
                if (!fallbackFrameLogged) {
                    qDebug() << "MediaFoundationPlayer extractFrame (fallback): videoSize:" << m_videoSize
                             << "m_stride:" << m_stride << "minStride:" << minStride
                             << "strideFromMax:" << fromMax.stride << "maxHeight:" << fromMax.usedHeight
                             << "strideFromCurrent:" << fromCurrent.stride << "currentHeight:" << fromCurrent.usedHeight
                             << "strideToUse:" << strideToUse << "exactStride:" << chosen.exact
                             << "currentLength:" << currentLength << "maxLength:" << maxLength
                             << "expectedSize:" << expectedSize;
                fallbackFrameLogged = true;
            }

                if (strideToUse <= 0) {
                    buffer->Unlock();
                    buffer->Release();
                    return QImage();
                }

                if (m_stride != signedStride) {
                    m_stride = signedStride;
                }

                if (height > 0 && maxLength >= static_cast<DWORD>(height * strideToUse)) {
                    frame = QImage(m_videoSize.width(), m_videoSize.height(), QImage::Format_RGB32);

                    if (!frame.isNull()) {
                        if (m_stride < 0) {
                            for (int y = 0; y < m_videoSize.height(); y++) {
                                const BYTE *srcRow = data + (m_videoSize.height() - 1 - y) * strideToUse;
                                memcpy(frame.scanLine(y), srcRow, m_videoSize.width() * 4);
                            }
                        } else {
                            for (int y = 0; y < m_videoSize.height(); y++) {
                                const BYTE *srcRow = data + y * strideToUse;
                                memcpy(frame.scanLine(y), srcRow, m_videoSize.width() * 4);
                            }
                        }
                    }
                }
                buffer->Unlock();
            }
        }

        buffer->Release();
        return frame;
    }

    IMFSourceReader *m_reader = nullptr;
    QSize m_videoSize;
    int m_stride = 0;
    int m_frameIntervalMs = 33;
    float m_playbackRate = 1.0f;

    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_paused{true};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<qint64> m_seekPosition{0};
};

// MediaFoundationPlayer implementation using IMFSourceReader with background thread
class MediaFoundationPlayer : public IVideoPlayer
{
    Q_OBJECT

public:
    explicit MediaFoundationPlayer(QObject *parent = nullptr);
    ~MediaFoundationPlayer() override;

    // IVideoPlayer interface
    bool load(const QString &filePath) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;

    State state() const override { return m_state; }
    qint64 duration() const override { return m_duration; }
    qint64 position() const override { return m_position; }
    QSize videoSize() const override { return m_videoSize; }
    bool hasVideo() const override { return m_hasVideo; }

    void setVolume(float volume) override { m_volume = qBound(0.0f, volume, 1.0f); }
    float volume() const override { return m_volume; }
    void setMuted(bool muted) override { m_muted = muted; }
    bool isMuted() const override { return m_muted; }

    void setLooping(bool loop) override { m_looping = loop; }
    bool isLooping() const override { return m_looping; }

    void setPlaybackRate(float rate) override;
    float playbackRate() const override { return m_playbackRate; }

private slots:
    void onFrameReady(const QImage &frame, qint64 timestampMs);
    void onEndOfStream();
    void onReaderError(const QString &message);

private:
    void cleanup();
    bool createSourceReader(const QString &filePath);
    bool configureVideoOutput();
    bool getMediaDuration();
    bool readFirstFrame();
    void setState(State newState);
    void stopReaderThread();

    // Media Foundation objects
    IMFSourceReader *m_reader = nullptr;

    // Worker thread
    FrameReaderThread *m_readerThread = nullptr;

    // Position timer
    QTimer *m_positionTimer = nullptr;

    // State
    State m_state = State::Stopped;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    QSize m_videoSize;
    bool m_hasVideo = false;
    float m_volume = 1.0f;
    bool m_muted = false;
    bool m_looping = false;
    float m_playbackRate = 1.0f;
    bool m_mfInitialized = false;

    // Frame timing
    int m_frameIntervalMs = 33;
    qint64 m_playbackStartTime = 0;
    qint64 m_playbackStartPosition = 0;

    // Stride for RGB32 frame data
    int m_stride = 0;
};

MediaFoundationPlayer::MediaFoundationPlayer(QObject *parent)
    : IVideoPlayer(parent)
    , m_positionTimer(new QTimer(this))
{
    HRESULT hr = MFStartup(MF_VERSION);
    m_mfInitialized = SUCCEEDED(hr);
    if (!m_mfInitialized) {
        qWarning() << "MediaFoundationPlayer: Failed to initialize Media Foundation:" << Qt::hex << hr;
    }

    connect(m_positionTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::Playing && m_duration > 0) {
            emit positionChanged(m_position);
        }
    });
    m_positionTimer->setInterval(100);
}

MediaFoundationPlayer::~MediaFoundationPlayer()
{
    stopReaderThread();
    m_positionTimer->stop();
    cleanup();
    if (m_mfInitialized) {
        MFShutdown();
    }
}

void MediaFoundationPlayer::stopReaderThread()
{
    if (m_readerThread) {
        m_readerThread->requestStop();
        m_readerThread->wait(1000);
        if (m_readerThread->isRunning()) {
            m_readerThread->terminate();
            m_readerThread->wait(500);
        }
        delete m_readerThread;
        m_readerThread = nullptr;
    }
}

void MediaFoundationPlayer::cleanup()
{
    stopReaderThread();

    if (m_reader) {
        m_reader->Release();
        m_reader = nullptr;
    }

    m_hasVideo = false;
    m_duration = 0;
    m_position = 0;
    m_videoSize = QSize();
    m_stride = 0;
}

bool MediaFoundationPlayer::load(const QString &filePath)
{
    cleanup();

    if (!m_mfInitialized) {
        emit error("Media Foundation not initialized");
        return false;
    }

    qDebug() << "MediaFoundationPlayer: Loading" << filePath;

    if (!createSourceReader(filePath)) {
        emit error("Failed to create source reader");
        return false;
    }

    if (!configureVideoOutput()) {
        emit error("Failed to configure video output");
        cleanup();
        return false;
    }

    if (!getMediaDuration()) {
        qWarning() << "MediaFoundationPlayer: Could not get media duration";
    }

    qDebug() << "MediaFoundationPlayer: Media loaded, duration:" << m_duration
             << "size:" << m_videoSize;

    m_position = 0;
    emit durationChanged(m_duration);
    emit positionChanged(m_position);

    // Read first frame synchronously (small blocking call, acceptable at load time)
    readFirstFrame();

    // Create reader thread
    m_readerThread = new FrameReaderThread(this);
    m_readerThread->setReader(m_reader);
    m_readerThread->setVideoSize(m_videoSize);
    m_readerThread->setStride(m_stride);
    m_readerThread->setFrameInterval(m_frameIntervalMs);

    connect(m_readerThread, &FrameReaderThread::frameReady,
            this, &MediaFoundationPlayer::onFrameReady, Qt::QueuedConnection);
    connect(m_readerThread, &FrameReaderThread::endOfStream,
            this, &MediaFoundationPlayer::onEndOfStream, Qt::QueuedConnection);
    connect(m_readerThread, &FrameReaderThread::errorOccurred,
            this, &MediaFoundationPlayer::onReaderError, Qt::QueuedConnection);

    m_readerThread->start();

    emit mediaLoaded();
    return true;
}

bool MediaFoundationPlayer::createSourceReader(const QString &filePath)
{
    QString resolvedPath = filePath;
    if (filePath.startsWith("file://", Qt::CaseInsensitive)) {
        QUrl url(filePath);
        if (url.isLocalFile()) {
            resolvedPath = url.toLocalFile();
        }
    }
    resolvedPath = QDir::toNativeSeparators(resolvedPath);

    IMFAttributes *attributes = nullptr;
    HRESULT hr = MFCreateAttributes(&attributes, 3);
    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to create attributes:" << Qt::hex << hr;
        return false;
    }

    // Enable video processing for format conversion
    hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to enable video processing:" << Qt::hex << hr;
    }

    // Prefer software decode for stable system-memory buffers and valid 2D pitch.
    hr = attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to disable DXVA:" << Qt::hex << hr;
    }

    hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to disable hardware transforms:" << Qt::hex << hr;
    }

    hr = MFCreateSourceReaderFromURL(
        reinterpret_cast<LPCWSTR>(resolvedPath.utf16()),
        attributes,
        &m_reader);

    attributes->Release();

    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to create source reader:" << Qt::hex << hr;
        return false;
    }

    return true;
}

bool MediaFoundationPlayer::configureVideoOutput()
{
    if (!m_reader) return false;

    IMFMediaType *nativeType = nullptr;
    HRESULT hr = m_reader->GetNativeMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        &nativeType);

    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: No video stream found:" << Qt::hex << hr;
        return false;
    }

    UINT32 width = 0, height = 0;
    hr = MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &width, &height);
    if (SUCCEEDED(hr) && width > 0 && height > 0) {
        m_videoSize = QSize(width, height);
        m_hasVideo = true;
    }

    UINT32 numerator = 0, denominator = 0;
    hr = MFGetAttributeRatio(nativeType, MF_MT_FRAME_RATE, &numerator, &denominator);
    if (SUCCEEDED(hr) && numerator > 0 && denominator > 0) {
        double fps = static_cast<double>(numerator) / denominator;
        m_frameIntervalMs = static_cast<int>(1000.0 / fps);
        qDebug() << "MediaFoundationPlayer: Frame rate:" << fps << "fps";
    }

    nativeType->Release();

    if (!m_hasVideo) {
        qWarning() << "MediaFoundationPlayer: Could not get video dimensions";
        return false;
    }

    // Configure RGB32 output
    IMFMediaType *outputType = nullptr;
    hr = MFCreateMediaType(&outputType);
    if (FAILED(hr)) return false;

    hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) { outputType->Release(); return false; }

    hr = outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(hr)) { outputType->Release(); return false; }

    hr = m_reader->SetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        nullptr,
        outputType);

    outputType->Release();

    if (FAILED(hr)) {
        qWarning() << "MediaFoundationPlayer: Failed to set output type:" << Qt::hex << hr;
        return false;
    }

    // Get stride - MF_MT_DEFAULT_STRIDE is signed (negative = bottom-up buffer)
    IMFMediaType *actualType = nullptr;
    hr = m_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType);
    if (SUCCEEDED(hr)) {
        UINT32 outWidth = 0, outHeight = 0;
        if (SUCCEEDED(MFGetAttributeSize(actualType, MF_MT_FRAME_SIZE, &outWidth, &outHeight))
            && outWidth > 0 && outHeight > 0) {
            QSize outputSize(outWidth, outHeight);
            if (m_videoSize != outputSize) {
                qDebug() << "MediaFoundationPlayer: Output frame size:" << outputSize;
            }
            m_videoSize = outputSize;
            m_hasVideo = true;
        }

        UINT32 strideRaw = 0;
        hr = actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, &strideRaw);
        qDebug() << "MediaFoundationPlayer: GetUINT32(MF_MT_DEFAULT_STRIDE) hr:" << Qt::hex << hr
                 << "strideRaw:" << strideRaw << "as INT32:" << static_cast<INT32>(strideRaw);
        if (SUCCEEDED(hr)) {
            m_stride = static_cast<INT32>(strideRaw);
        } else {
            // Fallback: calculate stride using MFGetStrideForBitmapInfoHeader
            LONG calcStride = 0;
            hr = MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, m_videoSize.width(), &calcStride);
            qDebug() << "MediaFoundationPlayer: MFGetStrideForBitmapInfoHeader hr:" << Qt::hex << hr
                     << "calcStride:" << calcStride;
            m_stride = SUCCEEDED(hr) ? calcStride : m_videoSize.width() * 4;
        }
        actualType->Release();
    } else {
        qDebug() << "MediaFoundationPlayer: GetCurrentMediaType failed hr:" << Qt::hex << hr;
        m_stride = m_videoSize.width() * 4;
    }

    qDebug() << "MediaFoundationPlayer: Configured RGB32, size:" << m_videoSize
             << "stride:" << m_stride << "width*4:" << (m_videoSize.width() * 4);
    return true;
}

bool MediaFoundationPlayer::getMediaDuration()
{
    if (!m_reader) return false;

    PROPVARIANT var;
    PropVariantInit(&var);

    HRESULT hr = m_reader->GetPresentationAttribute(
        MF_SOURCE_READER_MEDIASOURCE,
        MF_PD_DURATION,
        &var);

    if (SUCCEEDED(hr)) {
        m_duration = var.hVal.QuadPart / 10000;
        PropVariantClear(&var);
        return true;
    }

    PropVariantClear(&var);
    return false;
}

bool MediaFoundationPlayer::readFirstFrame()
{
    if (!m_reader) return false;

    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    IMFSample *sample = nullptr;

    HRESULT hr = m_reader->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        &streamIndex,
        &flags,
        &timestamp,
        &sample);

    if (FAILED(hr) || !sample) {
        return false;
    }

    // Extract and emit first frame
    IMFMediaBuffer *buffer = nullptr;
    hr = sample->GetBufferByIndex(0, &buffer);
    if (SUCCEEDED(hr)) {
        QImage frame;

        // Try IMF2DBuffer first to get actual stride
        IMF2DBuffer *buffer2D = nullptr;
        hr = buffer->QueryInterface(IID_PPV_ARGS(&buffer2D));
        if (SUCCEEDED(hr)) {
            BYTE *data = nullptr;
            LONG pitch = 0;
            hr = buffer2D->Lock2D(&data, &pitch);
            if (SUCCEEDED(hr)) {
                if (m_stride != static_cast<int>(pitch)) {
                    m_stride = static_cast<int>(pitch);
                }
                frame = QImage(m_videoSize.width(), m_videoSize.height(), QImage::Format_RGB32);
                if (!frame.isNull()) {
                    int absPitch = qAbs(pitch);

                    if (pitch < 0) {
                        for (int y = 0; y < m_videoSize.height(); y++) {
                            const BYTE *srcRow = data + (m_videoSize.height() - 1 - y) * absPitch;
                            memcpy(frame.scanLine(y), srcRow, m_videoSize.width() * 4);
                        }
                    } else {
                        for (int y = 0; y < m_videoSize.height(); y++) {
                            const BYTE *srcRow = data + y * absPitch;
                            memcpy(frame.scanLine(y), srcRow, m_videoSize.width() * 4);
                        }
                    }
                }
                buffer2D->Unlock2D();
            }
            buffer2D->Release();
        } else {
            // Fallback to regular buffer - calculate actual stride from buffer size
            BYTE *data = nullptr;
            DWORD maxLength = 0, currentLength = 0;

            hr = buffer->Lock(&data, &maxLength, &currentLength);
            if (SUCCEEDED(hr)) {
                int height = m_videoSize.height();
                int width = m_videoSize.width();
                int defaultStride = qAbs(m_stride);
                StrideResult fromMax = computeStrideFromLength(width, height, maxLength, defaultStride);
                StrideResult fromCurrent = computeStrideFromLength(width, height, currentLength, defaultStride);
                StrideResult chosen = fromCurrent;
                if (chosen.stride <= 0 || (!chosen.exact && fromMax.exact)) {
                    chosen = fromMax;
                }
                int strideToUse = chosen.stride;
                int strideSign = (m_stride < 0) ? -1 : 1;
                int signedStride = strideSign * strideToUse;

                if (strideToUse > 0) {
                    if (m_stride != signedStride) {
                        m_stride = signedStride;
                    }

                    if (height > 0 && maxLength >= static_cast<DWORD>(height * strideToUse)) {
                        frame = QImage(m_videoSize.width(), m_videoSize.height(), QImage::Format_RGB32);

                        if (!frame.isNull()) {
                            if (m_stride < 0) {
                                for (int y = 0; y < m_videoSize.height(); y++) {
                                    const BYTE *srcRow = data + (m_videoSize.height() - 1 - y) * strideToUse;
                                    memcpy(frame.scanLine(y), srcRow, m_videoSize.width() * 4);
                                }
                            } else {
                                for (int y = 0; y < m_videoSize.height(); y++) {
                                    const BYTE *srcRow = data + y * strideToUse;
                                    memcpy(frame.scanLine(y), srcRow, m_videoSize.width() * 4);
                                }
                            }
                        }
                    }
                }
                buffer->Unlock();
            }
        }

        if (!frame.isNull()) {
            emit frameReady(frame);
        }
        buffer->Release();
    }
    sample->Release();

    // Seek back to beginning for playback
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = 0;
    m_reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);

    return true;
}

void MediaFoundationPlayer::play()
{
    qDebug() << "MediaFoundationPlayer::play()";

    if (!m_readerThread || !m_hasVideo) return;

    m_playbackStartTime = QDateTime::currentMSecsSinceEpoch();
    m_playbackStartPosition = m_position;

    m_readerThread->setPlaybackRate(m_playbackRate);
    m_readerThread->requestResume();
    m_positionTimer->start();
    setState(State::Playing);
}

void MediaFoundationPlayer::pause()
{
    qDebug() << "MediaFoundationPlayer::pause()";

    if (m_readerThread) {
        m_readerThread->requestPause();
    }
    m_positionTimer->stop();
    setState(State::Paused);
}

void MediaFoundationPlayer::stop()
{
    qDebug() << "MediaFoundationPlayer::stop()";

    if (m_readerThread) {
        m_readerThread->requestPause();
        m_readerThread->requestSeek(0);
    }
    m_positionTimer->stop();
    m_position = 0;
    emit positionChanged(0);
    setState(State::Stopped);
}

void MediaFoundationPlayer::seek(qint64 positionMs)
{
    qDebug() << "MediaFoundationPlayer::seek() to" << positionMs << "ms";

    positionMs = qBound(0LL, positionMs, m_duration);
    m_position = positionMs;
    m_playbackStartPosition = positionMs;
    m_playbackStartTime = QDateTime::currentMSecsSinceEpoch();

    if (m_readerThread) {
        m_readerThread->requestSeek(positionMs);
    }

    emit positionChanged(positionMs);
}

void MediaFoundationPlayer::setPlaybackRate(float rate)
{
    float newRate = qBound(0.25f, rate, 2.0f);
    if (m_playbackRate == newRate) return;

    m_playbackRate = newRate;
    if (m_readerThread) {
        m_readerThread->setPlaybackRate(newRate);
    }
    emit playbackRateChanged(newRate);
}

void MediaFoundationPlayer::onFrameReady(const QImage &frame, qint64 timestampMs)
{
    m_position = timestampMs;
    emit frameReady(frame);
}

void MediaFoundationPlayer::onEndOfStream()
{
    qDebug() << "MediaFoundationPlayer: End of stream";

    if (m_looping) {
        seek(0);
        play();
    } else {
        m_positionTimer->stop();
        setState(State::Stopped);
        m_position = m_duration;
        emit positionChanged(m_duration);
        emit playbackFinished();
    }
}

void MediaFoundationPlayer::onReaderError(const QString &message)
{
    qWarning() << "MediaFoundationPlayer: Reader error:" << message;
    emit error(message);
}

void MediaFoundationPlayer::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
    }
}

// Factory function implementations
IVideoPlayer* createMediaFoundationPlayer(QObject *parent)
{
    return new MediaFoundationPlayer(parent);
}

bool isMediaFoundationAvailable()
{
    HRESULT hr = MFStartup(MF_VERSION);
    if (SUCCEEDED(hr)) {
        MFShutdown();
        return true;
    }
    return false;
}

#include "MediaFoundationPlayer_win.moc"

#endif // Q_OS_WIN
