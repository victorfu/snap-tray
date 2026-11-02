#ifndef FRAMEPACER_H
#define FRAMEPACER_H

#include <QtGlobal>

namespace SnapTray {

/**
 * Keeps accepted frames at no more than the target frame rate and tells
 * late frames apart from real stalls.
 *
 * A frame closer than 60 % of an interval to the last emitted one is
 * dropped (over-delivery, for example a 60 fps source recorded at 30 fps);
 * consecutive frames may still come up to 40 % of an interval early
 * (about ±6 ms of jitter at 30 fps). Frames keep
 * their real capture time: the pacer never
 * invents frames or moves timestamps. The encoders are variable-frame-rate,
 * so a static screen simply holds the last frame; nothing is synthesized
 * ahead of a real frame's time.
 *
 * Gap classification, for health metrics:
 *   - late: the gap was longer than one interval but shorter than the stall
 *     threshold (a frame arrived late, the cadence recovers);
 *   - stall: the gap reached the threshold (capture stopped delivering).
 *
 * Used for backends that capture on their own thread; GUI-timer polling is
 * already paced by its timer. Not thread-safe; callers serialize access
 * (RecordingManager uses the same lock as the session clock).
 */
class FramePacer
{
public:
    enum class Decision { Emit, DropTooSoon };

    struct Stats {
        qint64 emitted = 0;
        qint64 dropped = 0;
        qint64 late = 0;
        qint64 stalls = 0;
        qint64 longestGapNs = 0;
    };

    static constexpr int kStallIntervals = 4;

    explicit FramePacer(int framesPerSecond = 30);
    void reset(int framesPerSecond);

    // `recordingTimeNs` must be non-decreasing (SessionClock guarantees it).
    Decision onFrame(qint64 recordingTimeNs);

    // A pause is not a stall: the gap across it is not classified.
    void notePause();

    qint64 frameIntervalNs() const { return m_intervalNs; }
    Stats stats() const { return m_stats; }

private:
    qint64 m_intervalNs = 0;
    qint64 m_lastEmittedNs = -1;
    bool m_pauseBoundary = false;
    Stats m_stats;
};

} // namespace SnapTray

#endif // FRAMEPACER_H
