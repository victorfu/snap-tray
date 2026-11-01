#include "recording/FramePacer.h"

namespace SnapTray {

namespace {
constexpr qint64 kNsPerSecond = 1000000000;
constexpr int kFallbackFramesPerSecond = 30;
// Closer than this share of an interval is over-delivery. 60 % halves a
// 60 fps source at 30 fps and still lets consecutive frames come up to
// 40 % of an interval early (about ±6 ms at 30 fps).
constexpr qint64 kMinSpacingPercent = 60;
}

FramePacer::FramePacer(int framesPerSecond)
{
    reset(framesPerSecond);
}

void FramePacer::reset(int framesPerSecond)
{
    m_intervalNs = kNsPerSecond / (framesPerSecond > 0 ? framesPerSecond : kFallbackFramesPerSecond);
    m_lastEmittedNs = -1;
    m_pauseBoundary = false;
    m_stats = {};
}

FramePacer::Decision FramePacer::onFrame(qint64 recordingTimeNs)
{
    if (m_lastEmittedNs >= 0) {
        const qint64 gap = recordingTimeNs - m_lastEmittedNs;
        if (gap < m_intervalNs * kMinSpacingPercent / 100) {
            ++m_stats.dropped;
            return Decision::DropTooSoon;
        }
        if (!m_pauseBoundary) {
            m_stats.longestGapNs = qMax(m_stats.longestGapNs, gap);
            // Over half an interval past the expected time counts as late.
            if (gap >= kStallIntervals * m_intervalNs) {
                ++m_stats.stalls;
            } else if (gap > m_intervalNs + m_intervalNs / 2) {
                ++m_stats.late;
            }
        }
    }
    m_pauseBoundary = false;
    m_lastEmittedNs = recordingTimeNs;
    ++m_stats.emitted;
    return Decision::Emit;
}

void FramePacer::notePause()
{
    // Keep the spacing rule (the clock removes the pause, so times continue),
    // but do not count the pause as a gap.
    m_pauseBoundary = true;
}

} // namespace SnapTray
