// Define MSF_GIF_IMPL before including the header
// This is the only translation unit that includes the implementation
#define MSF_GIF_IMPL
#include "external/msf_gif.h"

#include "encoding/NativeGifEncoder.h"
#include <QDebug>
#include <QSaveFile>
#include <limits>

// Helper macro for casting opaque pointer
#define GIF_STATE static_cast<MsfGifState*>(m_gifState)

NativeGifEncoder::NativeGifEncoder(QObject *parent)
    : QObject(parent)
    , m_gifState(nullptr)
    , m_frameRate(30)
    , m_maxBitDepth(16)
    , m_framesWritten(0)
    , m_pendingTimestampMs(-1)
    , m_delayRemainderUnits(0)
    , m_running(false)
    , m_aborted(false)
{
}

NativeGifEncoder::~NativeGifEncoder()
{
    cleanup();
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
    m_bufferedBytes->store(0);
    m_writtenBytes->store(0);
    m_finishEmitted = false;
    m_pendingFrame = QImage();
    m_pendingTimestampMs = -1;
    m_delayRemainderUnits = 0;
    m_lastError.clear();
    m_aborted = false;

    m_file = std::make_unique<QSaveFile>(m_outputPath, this);
    m_file->setDirectWriteFallback(false);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        fail("Failed to open output file: " + m_file->errorString());
        return false;
    }

    // Allocate and initialize msf_gif state
    MsfGifState *state = new MsfGifState();
    *state = {};  // Zero-initialize
    m_gifState = state;

    if (!msf_gif_begin_to_file(GIF_STATE, m_frameSize.width(), m_frameSize.height(),
                               &NativeGifEncoder::writeToFile, this)) {
        fail("Failed to initialize GIF encoder");
        return false;
    }
    m_bufferedBytes->store(qint64(GIF_STATE->listHead->size) + 1); // header + trailer

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

    const bool hasComparableTimestamps = timestampMs >= 0 && m_pendingTimestampMs >= 0;
    const bool timestampRegressed = hasComparableTimestamps
        && timestampMs <= m_pendingTimestampMs;
    const qint64 durationMs = hasComparableTimestamps && !timestampRegressed
        ? timestampMs - m_pendingTimestampMs
        : -1;
    const qint64 nextPendingTimestampMs = timestampRegressed ? -1 : timestampMs;
    const int centiSeconds = calculateCentiseconds(durationMs);

    if (!encodePendingFrame(centiSeconds)) {
        return;
    }

    m_pendingFrame = processed.copy();
    m_pendingTimestampMs = nextPendingTimestampMs;
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

    const int result = msf_gif_frame_to_file(
        GIF_STATE,
        const_cast<uint8_t *>(m_pendingFrame.constBits()),
        centiSeconds,
        m_maxBitDepth,
        m_pendingFrame.bytesPerLine());
    if (result) {
        // The official API retains the last packet so the next frame can
        // update its disposal. Already-written packets must not be counted.
        m_bufferedBytes->store(qint64(GIF_STATE->listHead->size) + 1);
        return true;
    }

    // msf frees its state on either allocation or write failure; retrying
    // cannot recover it. Cancel the atomic output immediately.
    fail(m_lastError.isEmpty() ? QStringLiteral("Failed to encode GIF frame") : m_lastError);
    return false;
}

size_t NativeGifEncoder::writeToFile(const void *buffer, size_t size, size_t count, void *context)
{
    auto *encoder = static_cast<NativeGifEncoder *>(context);
    if (!buffer || !size || !count || !encoder->m_file
        || size > size_t(std::numeric_limits<qint64>::max()) / count) {
        encoder->m_lastError = "GIF encoder produced invalid output";
        return 0;
    }
    const qint64 bytes = static_cast<qint64>(size * count);
    const qint64 written = encoder->m_file->write(static_cast<const char *>(buffer), bytes);
    if (written > 0) {
        encoder->m_writtenBytes->fetch_add(written);
    }
    if (written != bytes) {
        encoder->m_lastError = QString("Failed to write GIF data: wrote %1 of %2 bytes: %3")
            .arg(written).arg(bytes).arg(encoder->m_file->errorString());
        return 0;
    }
    return count;
}

void NativeGifEncoder::fail(const QString &message)
{
    m_lastError = message;
    cleanup();
    emit error(m_lastError);
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
    if (m_finishEmitted) {
        return;
    }
    m_finishEmitted = true;
    if (!m_gifState) {
        emit finished(false, m_outputPath);
        return;
    }
    if (!m_running || m_framesWritten == 0 || m_pendingFrame.isNull()) {
        fail("GIF encoding produced no frames");
        emit finished(false, m_outputPath);
        return;
    }

    // No later timestamp exists: preserve the fractional base interval.
    if (!encodePendingFrame(calculateCentiseconds(-1))) {
        emit finished(false, m_outputPath);
        return;
    }
    if (!msf_gif_end_to_file(GIF_STATE)) {
        fail(m_lastError.isEmpty() ? QStringLiteral("Failed to finalize GIF data") : m_lastError);
        emit finished(false, m_outputPath);
        return;
    }
    if (!m_file->commit()) {
        fail("Failed to commit GIF data: " + m_file->errorString());
        emit finished(false, m_outputPath);
        return;
    }
    cleanup();
    emit finished(true, m_outputPath);
}

void NativeGifEncoder::abort()
{
    m_aborted = true;
    cleanup();
}

void NativeGifEncoder::cleanup()
{
    if (m_gifState) {
        // listHead is msf's validity marker. Failed calls and end already
        // freed the buffers; their other pointer fields may still dangle.
        if (GIF_STATE->listHead) {
            msf_free_gif_state(GIF_STATE);
        }
        delete GIF_STATE;
        m_gifState = nullptr;
    }
    if (m_file) {
        if (m_file->isOpen()) {
            m_file->cancelWriting();
        }
        m_file.reset();
    }
    m_bufferedBytes->store(0);
    m_pendingFrame = QImage();
    m_pendingTimestampMs = -1;
    m_delayRemainderUnits = 0;
    m_running = false;
}
