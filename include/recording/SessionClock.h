#ifndef SESSIONCLOCK_H
#define SESSIONCLOCK_H

#include <QMutex>
#include <QVector>
#include <QtGlobal>

namespace SnapTray {

/**
 * Maps capture-clock times (nanoseconds, monotonic) to recording time.
 *
 * - Start gate: nothing captured before start() belongs to the recording,
 *   so frames from preparation or the countdown never leak in.
 * - Pauses: time between pause() and resume() is removed. A frame captured
 *   inside a pause is rejected; frames after it continue seamlessly.
 * - Order: a frame whose recording time does not advance past the last
 *   accepted one is rejected and counted (duplicate or backward), rather
 *   than being nudged forward and shifting later frames.
 *
 * Times stay in nanoseconds; callers convert once, at the encoder boundary
 * (toEncoderMs), instead of truncating to milliseconds along the way. Video
 * frames, the window timeline and the duration shown to the user use the
 * same mapping. Audio keeps its sample timeline (TimestampedPcmMixer) and
 * starts and pauses at the same moments.
 *
 * Thread-safe: frames are admitted on capture threads while pause and resume
 * come from the GUI thread.
 */
class SessionClock
{
public:
    enum class Admission { Accepted, BeforeStart, Paused, Duplicate, Backward, Stopped };

    struct Stats {
        qint64 accepted = 0;
        qint64 beforeStart = 0;
        qint64 paused = 0;
        qint64 duplicates = 0;
        qint64 backwards = 0;
    };

    // Opens the gate: recording time 0 is capture time `epochNs`.
    void start(qint64 epochNs);
    void pause(qint64 atNs);
    void resume(qint64 atNs);
    // Closes the gate; later frames are rejected as Stopped.
    void stop();
    void reset();

    bool isStarted() const;
    bool isPaused() const;

    // Classifies a frame and, when accepted, returns its recording time.
    Admission admit(qint64 sourceTimeNs, qint64* recordingTimeNs);

    // Recording time at capture time `nowNs` (frozen while paused); 0 before start.
    qint64 recordingTimeAt(qint64 nowNs) const;

    Stats stats() const;

    // One rounding step from nanoseconds to the encoders' milliseconds.
    static qint64 toEncoderMs(qint64 recordingTimeNs);

private:
    struct PauseSpan {
        qint64 startNs;
        qint64 endNs;  // -1 while the pause is open
    };
    qint64 recordingTimeLocked(qint64 sourceTimeNs, bool* insidePause) const;

    mutable QMutex m_mutex;
    bool m_started = false;
    bool m_stopped = false;
    qint64 m_epochNs = 0;
    QVector<PauseSpan> m_pauses;
    qint64 m_lastAcceptedNs = -1;
    Stats m_stats;
};

} // namespace SnapTray

#endif // SESSIONCLOCK_H
