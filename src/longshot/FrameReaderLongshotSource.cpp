#include "longshot/FrameReaderLongshotSource.h"

#include "longshot/LongshotTypes.h"
#include "utils/VideoCropGeometry.h"
#include "video/IVideoFrameReader.h"

#include <QDebug>

#include <cmath>

namespace SnapTray::Longshot {

namespace {
constexpr double kMsPerSecond = 1000.0;
} // namespace

FrameReaderLongshotSource::FrameReaderLongshotSource(std::unique_ptr<IVideoFrameReader> reader)
    : m_reader(std::move(reader))
{
}

FrameReaderLongshotSource::~FrameReaderLongshotSource() = default;

std::unique_ptr<LongshotFrameSource> FrameReaderLongshotSource::createNative()
{
    auto reader = IVideoFrameReader::createOffline();
    if (!reader) {
        qWarning() << "FrameReaderLongshotSource: no video frame reader on this platform";
        return nullptr;
    }
    return std::make_unique<FrameReaderLongshotSource>(std::move(reader));
}

bool FrameReaderLongshotSource::open(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop)
{
    m_lastError.clear();
    m_nextIndex = 0;
    m_frameCount = 0;
    if (!m_reader) {
        m_lastError = QStringLiteral("No frame reader");
        return false;
    }
    if (!m_reader->load(path)) {
        m_lastError = m_reader->lastError().isEmpty() ? QStringLiteral("Failed to open %1").arg(path)
                                                      : m_reader->lastError();
        qWarning() << "FrameReaderLongshotSource:" << m_lastError;
        return false;
    }
    m_videoSize = m_reader->videoSize();
    const QRect frameRect(QPoint(0, 0), m_videoSize);
    if (!crop.isEmpty() && (crop.width() < kMinAnalysisSide || crop.height() < kMinAnalysisSide)) {
        m_lastError = QStringLiteral("crop %1x%2 is below the minimum %3 px side")
                          .arg(crop.width()).arg(crop.height()).arg(kMinAnalysisSide);
        return false;
    }
    // normalizeCropRect returns an empty rect both for "no crop" and for a
    // crop that covers the whole frame; either way the whole frame is used.
    const QRect normalized = crop.isEmpty() ? QRect() : VideoCropGeometry::normalizeCropRect(crop, m_videoSize);
    m_crop = normalized.isEmpty() ? frameRect : normalized;
    if (m_crop.width() < kMinAnalysisSide || m_crop.height() < kMinAnalysisSide) {
        m_lastError = QStringLiteral("crop %1x%2 is below the minimum %3 px side")
                          .arg(m_crop.width()).arg(m_crop.height()).arg(kMinAnalysisSide);
        return false;
    }
    m_frameRate = m_reader->frameRate() > 0.0 ? m_reader->frameRate() : 30.0;
    const qint64 duration = m_reader->duration();
    m_startMs = qBound<qint64>(0, startMs, duration);
    m_endMs = endMs < 0 ? duration : qBound<qint64>(m_startMs, endMs, duration);
    const double frameIntervalMs = kMsPerSecond / m_frameRate;
    m_frameCount = int(std::ceil(double(m_endMs - m_startMs) / frameIntervalMs));
    if (m_frameCount < 0) m_frameCount = 0;
    return true;
}

std::optional<QImage> FrameReaderLongshotSource::next(qint64* tMs)
{
    if (!m_reader || m_nextIndex >= m_frameCount) return std::nullopt;
    const double frameIntervalMs = kMsPerSecond / m_frameRate;
    const qint64 t = m_startMs + qint64(std::llround(m_nextIndex * frameIntervalMs));
    if (t >= m_endMs) {
        m_nextIndex = m_frameCount;
        return std::nullopt;
    }
    ++m_nextIndex;
    QImage frame = m_reader->frameAt(t);
    if (frame.isNull()) {
        m_lastError = m_reader->lastError();
        qWarning() << "FrameReaderLongshotSource: decode failed at" << t << "ms:" << m_lastError;
        return std::nullopt;
    }
    if (frame.format() != QImage::Format_RGB32) frame = frame.convertToFormat(QImage::Format_RGB32);
    if (m_crop != QRect(QPoint(0, 0), frame.size())) frame = frame.copy(m_crop);
    if (tMs) *tMs = t;
    return frame;
}

} // namespace SnapTray::Longshot
