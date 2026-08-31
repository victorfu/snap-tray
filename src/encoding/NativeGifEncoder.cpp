// Define MSF_GIF_IMPL before including the header
// This is the only translation unit that includes the implementation
#define MSF_GIF_IMPL
#include "external/msf_gif.h"

#include "encoding/NativeGifEncoder.h"
#include <QDebug>
#include <QSaveFile>

// Helper macro for casting opaque pointer
#define GIF_STATE static_cast<MsfGifState*>(m_gifState)

NativeGifEncoder::NativeGifEncoder(QObject *parent)
    : QObject(parent)
    , m_gifState(nullptr)
    , m_frameRate(30)
    , m_maxBitDepth(16)
    , m_framesWritten(0)
    , m_consecutiveFailures(0)
    , m_maxConsecutiveFailures(5)
    , m_pendingTimestampMs(-1)
    , m_delayRemainderUnits(0)
    , m_running(false)
    , m_aborted(false)
{
}

NativeGifEncoder::~NativeGifEncoder()
{
    if (m_running) {
        abort();
    }
}

void NativeGifEncoder::setMaxBitDepth(int depth)
{
    m_maxBitDepth = qBound(1, depth, 16);
}

bool NativeGifEncoder::start(const QString &outputPath, const QSize &frameSize, int frameRate)
{
    if (m_running) {
        m_lastError = "Encoder already running";
        return false;
    }

    // Validate minimum frame dimensions (must be at least 2x2 after rounding to even)
    if (frameSize.width() < 2 || frameSize.height() < 2) {
        m_lastError = QString("Frame size too small: %1x%2 (minimum 2x2)")
            .arg(frameSize.width()).arg(frameSize.height());
        emit error(m_lastError);
        return false;
    }

    m_outputPath = outputPath;
    // Ensure dimensions are even (required by some codecs/formats)
    m_frameSize = QSize(
        (frameSize.width() + 1) & ~1,
        (frameSize.height() + 1) & ~1
    );
    m_frameRate = qBound(1, frameRate, 240);
    m_framesWritten = 0;
    m_consecutiveFailures = 0;
    m_pendingFrame = QImage();
    m_pendingTimestampMs = -1;
    m_delayRemainderUnits = 0;
    m_lastError.clear();
    m_aborted = false;

    // Allocate and initialize msf_gif state
    MsfGifState *state = new MsfGifState();
    *state = {};  // Zero-initialize
    m_gifState = state;

    if (!msf_gif_begin(GIF_STATE, m_frameSize.width(), m_frameSize.height())) {
        m_lastError = "Failed to initialize GIF encoder";
        delete GIF_STATE;
        m_gifState = nullptr;
        emit error(m_lastError);
        return false;
    }

    m_running = true;
    qDebug() << "NativeGifEncoder: Started -" << m_frameSize << "@" << m_frameRate << "fps";
    return true;
}

void NativeGifEncoder::writeFrame(const QImage &frame, qint64 timestampMs)
{
    if (!m_running || m_aborted || !m_gifState) {
        qDebug() << "NativeGifEncoder::writeFrame - Skipping: running=" << m_running
                 << "aborted=" << m_aborted << "gifState=" << (m_gifState != nullptr);
        return;
    }

    if (frame.isNull()) {
        qWarning() << "NativeGifEncoder: Received null frame";
        return;
    }

    // Debug first few frames
    if (m_framesWritten < 3) {
        qDebug() << "NativeGifEncoder::writeFrame - Frame" << m_framesWritten
                 << "input size:" << frame.size()
                 << "format:" << frame.format()
                 << "timestamp:" << timestampMs;
    }

    // Process frame
    QImage processed = frame;

    // Scale if needed
    if (processed.size() != m_frameSize) {
        processed = processed.scaled(m_frameSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    // Convert to RGBA8888 format (required by msf_gif)
    if (processed.format() != QImage::Format_RGBA8888) {
        processed = processed.convertToFormat(QImage::Format_RGBA8888);
    }

    if (processed.isNull()) {
        qWarning() << "NativeGifEncoder: Frame conversion failed";
        return;
    }

    // Debug first few frames after processing
    if (m_framesWritten < 3) {
        qDebug() << "NativeGifEncoder::writeFrame - Processed frame" << m_framesWritten
                 << "size:" << processed.size()
                 << "format:" << processed.format()
                 << "bytesPerLine:" << processed.bytesPerLine()
                 << "expected:" << (processed.width() * 4);
    }

    // A GIF frame's delay describes how long that frame remains visible. Keep
    // one processed frame pending so the next timestamp can determine the
    // previous frame's delay instead of shifting every interval forward.
    if (m_pendingFrame.isNull()) {
        m_pendingFrame = processed.copy();
        m_pendingTimestampMs = timestampMs;
        m_framesWritten = 1;
        return;
    }

    const qint64 durationMs = timestampMs >= 0
            && m_pendingTimestampMs >= 0
            && timestampMs > m_pendingTimestampMs
        ? timestampMs - m_pendingTimestampMs
        : -1;
    const int centiSeconds = calculateCentiseconds(durationMs);

    if (!encodePendingFrame(centiSeconds)) {
        // Match the previous streaming behavior by dropping the failed frame
        // and allowing the current frame to become the next pending frame.
        // The failed interval was not emitted, so do not carry its remainder.
        m_delayRemainderUnits = 0;
        if (m_running) {
            m_pendingFrame = processed.copy();
            m_pendingTimestampMs = timestampMs;
        } else {
            m_pendingFrame = QImage();
            m_pendingTimestampMs = -1;
        }
        return;
    }

    m_pendingFrame = processed.copy();
    m_pendingTimestampMs = timestampMs;
    m_framesWritten++;

    // Emit progress every 30 frames
    if (m_framesWritten % 30 == 0) {
        emit progress(m_framesWritten);
    }
}

bool NativeGifEncoder::encodePendingFrame(int centiSeconds)
{
    if (!m_gifState || m_pendingFrame.isNull()) {
        return false;
    }

    const int result = msf_gif_frame(
        GIF_STATE,
        const_cast<uint8_t *>(m_pendingFrame.constBits()),
        centiSeconds,
        m_maxBitDepth,
        m_pendingFrame.bytesPerLine());
    if (result) {
        m_consecutiveFailures = 0;
        return true;
    }

    ++m_consecutiveFailures;
    qWarning() << "NativeGifEncoder: msf_gif_frame failed for frame"
               << qMax<qint64>(0, m_framesWritten - 1)
               << "(consecutive failures:" << m_consecutiveFailures << ")";

    if (m_consecutiveFailures >= m_maxConsecutiveFailures) {
        m_lastError = QString("GIF encoding failed: %1 consecutive frame failures")
            .arg(m_consecutiveFailures);
        m_running = false;
        emit error(m_lastError);
    }
    return false;
}

int NativeGifEncoder::calculateCentiseconds(qint64 durationMs)
{
    constexpr qint64 kMaxCentiseconds = 65535;
    constexpr qint64 kMaxDurationMs = kMaxCentiseconds * 10;

    if (durationMs > kMaxDurationMs) {
        m_delayRemainderUnits = 0;
        return static_cast<int>(kMaxCentiseconds);
    }

    // Use frameRate-scaled units so a default interval is exactly 1000/fps
    // milliseconds without losing its fractional part. Timestamp intervals
    // are exact integer milliseconds in the same unit system.
    const qint64 unitsPerCentisecond = static_cast<qint64>(m_frameRate) * 10;
    const qint64 durationUnits = durationMs > 0
        ? durationMs * m_frameRate
        : 1000;
    const qint64 totalUnits = m_delayRemainderUnits + durationUnits;
    const qint64 centiSeconds = totalUnits / unitsPerCentisecond;

    if (centiSeconds < 1) {
        // GIF delays below one centisecond cannot be represented. Reset the
        // remainder because the forced minimum already exceeds the duration.
        m_delayRemainderUnits = 0;
        return 1;
    }

    if (centiSeconds > kMaxCentiseconds) {
        m_delayRemainderUnits = 0;
        return static_cast<int>(kMaxCentiseconds);
    }

    m_delayRemainderUnits = totalUnits % unitsPerCentisecond;
    return static_cast<int>(centiSeconds);
}

void NativeGifEncoder::finish()
{
    qDebug() << "NativeGifEncoder::finish() - BEGIN, running=" << m_running
             << "gifState=" << (m_gifState != nullptr)
             << "framesWritten=" << m_framesWritten;

    if (!m_gifState) {
        qDebug() << "NativeGifEncoder::finish() - Early return, not running or no state";
        emit finished(false, QString());
        return;
    }

    if (!m_running || m_framesWritten == 0 || m_pendingFrame.isNull()) {
        MsfGifResult result = msf_gif_end(GIF_STATE);
        if (result.data) {
            msf_gif_free(result);
        }

        if (m_lastError.isEmpty()) {
            m_lastError = "GIF encoding produced no frames";
        }
        const QString outputPath = m_outputPath;
        cleanup();
        emit error(m_lastError);
        emit finished(false, outputPath);
        return;
    }

    qDebug() << "NativeGifEncoder: Finishing with" << m_framesWritten << "frames";

    // No later timestamp exists for the final frame, so use one exact base
    // frame interval and preserve any accumulated centisecond remainder.
    if (!encodePendingFrame(calculateCentiseconds(-1))) {
        MsfGifResult failedResult = msf_gif_end(GIF_STATE);
        if (failedResult.data) {
            msf_gif_free(failedResult);
        }
        if (m_lastError.isEmpty()) {
            m_lastError = "Failed to encode final GIF frame";
        }
        const QString outputPath = m_outputPath;
        cleanup();
        emit error(m_lastError);
        emit finished(false, outputPath);
        return;
    }
    m_pendingFrame = QImage();
    m_pendingTimestampMs = -1;

    MsfGifResult result = msf_gif_end(GIF_STATE);

    qDebug() << "NativeGifEncoder::finish() - msf_gif_end returned: data=" << result.data
             << "dataSize=" << result.dataSize;

    bool success = false;
    if (result.data && result.dataSize > 0) {
        QSaveFile file(m_outputPath);
        file.setDirectWriteFallback(false);
        qDebug() << "NativeGifEncoder::finish() - Opening file:" << m_outputPath;
        if (file.open(QIODevice::WriteOnly)) {
            qint64 written = file.write(
                reinterpret_cast<const char*>(result.data),
                static_cast<qint64>(result.dataSize)
            );
            if (written == static_cast<qint64>(result.dataSize) && file.commit()) {
                success = true;
                qDebug() << "NativeGifEncoder: Saved" << result.dataSize << "bytes to" << m_outputPath;
            } else {
                m_lastError = written == static_cast<qint64>(result.dataSize)
                    ? QString("Failed to commit GIF data: %1").arg(file.errorString())
                    : QString("Failed to write GIF data: wrote %1 of %2 bytes")
                          .arg(written).arg(result.dataSize);
                file.cancelWriting();
                qDebug() << "NativeGifEncoder::finish() - Write error:" << m_lastError;
            }
        } else {
            m_lastError = "Failed to open output file: " + file.errorString();
            qDebug() << "NativeGifEncoder::finish() - Open error:" << m_lastError;
        }

        msf_gif_free(result);
    } else {
        m_lastError = "GIF encoding produced no data";
        qDebug() << "NativeGifEncoder::finish() - No data produced!";
    }

    cleanup();

    if (success) {
        emit finished(true, m_outputPath);
    } else {
        emit error(m_lastError);
        emit finished(false, m_outputPath);
    }
}

void NativeGifEncoder::abort()
{
    if (!m_running) {
        return;
    }

    qDebug() << "NativeGifEncoder: Aborting";
    m_aborted = true;

    if (m_gifState) {
        // End encoding and discard result
        MsfGifResult result = msf_gif_end(GIF_STATE);
        if (result.data) {
            msf_gif_free(result);
        }
    }

    cleanup();
}

void NativeGifEncoder::cleanup()
{
    if (m_gifState) {
        delete GIF_STATE;
        m_gifState = nullptr;
    }
    m_pendingFrame = QImage();
    m_pendingTimestampMs = -1;
    m_delayRemainderUnits = 0;
    m_running = false;
}
