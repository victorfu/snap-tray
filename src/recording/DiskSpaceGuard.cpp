#include "recording/DiskSpaceGuard.h"

namespace SnapTray {

namespace {
constexpr qint64 kMsPerSecond = 1000;
constexpr qint64 kPercent = 100;
// Weight of a new sample in the moving average, in percent.
constexpr qint64 kRateSmoothingPercent = 30;
}

qint64 DiskSpaceGuard::safeStopThreshold(const Input& input)
{
    return kFloorBytes
        + input.recordedBytes * kFinalizePercent / kPercent
        + input.pendingBytes
        + input.bytesPerSecond * kSafeStopSeconds;
}

qint64 DiskSpaceGuard::warningThreshold(const Input& input)
{
    return safeStopThreshold(input)
        + input.bytesPerSecond * kWarningSeconds
        + (input.exportLikely ? input.recordedBytes : 0);
}

DiskSpaceGuard::Result DiskSpaceGuard::evaluate(const Input& input)
{
    Result result;
    result.safeStopBytes = safeStopThreshold(input);
    result.warningBytes = warningThreshold(input);
    if (m_level == Level::SafeStop) {
        // Once stopped, stay stopped for this session.
        result.level = Level::SafeStop;
        return result;
    }
    if (!input.freeBytes) {
        // Keep an earlier warning standing; otherwise say we cannot tell.
        m_level = m_level == Level::Warning ? Level::Warning : Level::Unknown;
        result.level = m_level;
        return result;
    }
    const qint64 freeBytes = *input.freeBytes;
    if (input.bytesPerSecond > 0) {
        result.secondsLeft = qMax<qint64>(0, (freeBytes - result.safeStopBytes) / input.bytesPerSecond);
    }
    if (freeBytes <= result.safeStopBytes) {
        m_level = Level::SafeStop;
    } else if (freeBytes <= result.warningBytes) {
        m_level = Level::Warning;
    } else if (m_level == Level::Warning
               && freeBytes <= result.warningBytes + kHysteresisBytes) {
        // Not clearly recovered yet.
        m_level = Level::Warning;
    } else {
        m_level = Level::Ok;
    }
    result.level = m_level;
    return result;
}

void WriteRateEstimator::addSample(qint64 fileBytes, qint64 timeMs)
{
    if (fileBytes < 0) {
        return;
    }
    if (m_lastTimeMs >= 0 && timeMs > m_lastTimeMs && fileBytes >= m_lastBytes) {
        const qint64 sample = (fileBytes - m_lastBytes) * kMsPerSecond / (timeMs - m_lastTimeMs);
        m_rate = m_rate == 0 ? sample
                             : (m_rate * (kPercent - kRateSmoothingPercent) + sample * kRateSmoothingPercent) / kPercent;
    }
    m_lastBytes = fileBytes;
    m_lastTimeMs = timeMs;
}

void WriteRateEstimator::reset()
{
    m_lastBytes = -1;
    m_lastTimeMs = -1;
    m_rate = 0;
}

} // namespace SnapTray
