// Define MSF_GIF_IMPL before including the header
// This is the only translation unit that includes the implementation
#define MSF_GIF_IMPL
#include "external/msf_gif.h"

#include "encoding/NativeGifEncoder.h"
#include <QDebug>
#include <QFile>

// Helper macro for casting opaque pointer
#define GIF_STATE static_cast<MsfGifState*>(m_gifState)

NativeGifEncoder::NativeGifEncoder(QObject *parent)
    : QObject(parent)
    , m_gifState(nullptr)
    , m_frameRate(30)
    , m_maxBitDepth(16)
    , m_framesWritten(0)
    , m_consecutiveFailures(0)
    , m_lastTimestampMs(0)
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
    m_frameRate = frameRate;
    m_framesWritten = 0;
    m_consecutiveFailures = 0;
    m_lastTimestampMs = 0;
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

    // Calculate delay in centiseconds (1/100 second)
    int centiSeconds = calculateCentiseconds(timestampMs);

    // Write frame to GIF
    int pitchInBytes = processed.bytesPerLine();
    int result = msf_gif_frame(GIF_STATE,
                       const_cast<uint8_t*>(processed.constBits()),
                       centiSeconds,
                       m_maxBitDepth,
                       pitchInBytes);
    if (!result) {
        m_consecutiveFailures++;
        qWarning() << "NativeGifEncoder: msf_gif_frame failed for frame" << m_framesWritten
                   << "(consecutive failures:" << m_consecutiveFailures << ")";

        // Stop encoding after too many consecutive failures to prevent silent corruption
        constexpr int MAX_CONSECUTIVE_FAILURES = 10;
        if (m_consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
            m_lastError = QString("GIF encoding failed: %1 consecutive frame failures")
                .arg(m_consecutiveFailures);
            m_running = false;
            emit error(m_lastError);
        }
        return;
    }

    // Reset failure counter on success
    m_consecutiveFailures = 0;
    m_framesWritten++;
    m_lastTimestampMs = timestampMs;

    // Emit progress every 30 frames
    if (m_framesWritten % 30 == 0) {
        emit progress(m_framesWritten);
    }
}

int NativeGifEncoder::calculateCentiseconds(qint64 timestampMs)
{
    if (timestampMs < 0 || m_framesWritten == 0) {
        // First frame or no timestamp provided, use base frame rate
        // Default to 10 centiseconds (100ms) if frameRate is invalid
        return (m_frameRate > 0) ? (100 / m_frameRate) : 10;
    }

    // Calculate time delta from previous frame
    qint64 deltaMs = timestampMs - m_lastTimestampMs;

    // Convert to centiseconds (1/100 second)
    int centiSeconds = static_cast<int>(deltaMs / 10);

    // GIF spec: delay of 0 causes some browsers to use default
    // Range: 1 (10ms) to 65535 (655.35 seconds)
    return qBound(1, centiSeconds, 65535);
}

void NativeGifEncoder::finish()
{
    qDebug() << "NativeGifEncoder::finish() - BEGIN, running=" << m_running
             << "gifState=" << (m_gifState != nullptr)
             << "framesWritten=" << m_framesWritten;

    if (!m_running || !m_gifState) {
        qDebug() << "NativeGifEncoder::finish() - Early return, not running or no state";
        return;
    }

    qDebug() << "NativeGifEncoder: Finishing with" << m_framesWritten << "frames";

    MsfGifResult result = msf_gif_end(GIF_STATE);

    qDebug() << "NativeGifEncoder::finish() - msf_gif_end returned: data=" << result.data
             << "dataSize=" << result.dataSize;

    bool success = false;
    if (result.data && result.dataSize > 0) {
        QFile file(m_outputPath);
        qDebug() << "NativeGifEncoder::finish() - Opening file:" << m_outputPath;
        if (file.open(QIODevice::WriteOnly)) {
            qint64 written = file.write(
                reinterpret_cast<const char*>(result.data),
                static_cast<qint64>(result.dataSize)
            );
            file.close();

            if (written == static_cast<qint64>(result.dataSize)) {
                success = true;
                qDebug() << "NativeGifEncoder: Saved" << result.dataSize << "bytes to" << m_outputPath;
            } else {
                m_lastError = QString("Failed to write GIF data: wrote %1 of %2 bytes")
                    .arg(written).arg(result.dataSize);
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

    // Remove incomplete output file
    if (!m_outputPath.isEmpty() && QFile::exists(m_outputPath)) {
        QFile::remove(m_outputPath);
    }
}

void NativeGifEncoder::cleanup()
{
    if (m_gifState) {
        delete GIF_STATE;
        m_gifState = nullptr;
    }
    m_running = false;
}
