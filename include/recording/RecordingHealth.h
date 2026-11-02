#ifndef RECORDINGHEALTH_H
#define RECORDINGHEALTH_H

#include <QMutex>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace SnapTray {

/**
 * Counters and timings for one recording, from capture to encoder.
 *
 * Layers, each a subset of the one before:
 *   offered   frames handed to the encoding queue
 *   queued    accepted by the queue (the rest were dropped: queue full,
 *             over the byte budget, or the queue was stale)
 *   encoded   accepted by the encoder (the rest were rejected by it)
 * The muxer works inside the encoder; encoded frames are the ones written.
 * Frames the session clock or the frame pacer turned away (before start,
 * paused, out of order, over-delivery) are recorded separately: they are
 * policy, not loss. A static screen that sends no frames is not a drop.
 *
 * Thread-safe. The snapshot and the diagnostics text contain no images,
 * audio, window titles or file paths.
 */
class RecordingHealth
{
public:
    enum class FrameDrop { QueueFull, QueueBytes, QueueStale, EncoderRejected };

    struct ClockCounters {
        qint64 beforeStart = 0;
        qint64 paused = 0;
        qint64 outOfOrder = 0;  // duplicate or backward capture times
        qint64 pacedOut = 0;    // over-delivery dropped by the frame pacer
        qint64 late = 0;
        qint64 stalls = 0;
        qint64 longestGapMs = 0;
    };

    struct Snapshot {
        QString backend;
        quint64 generation = 0;
        qint64 offered = 0;
        qint64 queued = 0;
        qint64 encoded = 0;
        qint64 droppedQueueFull = 0;
        qint64 droppedQueueBytes = 0;
        qint64 droppedQueueStale = 0;
        qint64 encoderRejected = 0;
        qint64 audioQueued = 0;
        qint64 audioDropped = 0;
        qint64 audioDroppedBytes = 0;
        int peakQueueDepth = 0;
        qint64 peakQueueBytes = 0;
        qint64 queueAgeP50Ms = 0;
        qint64 queueAgeP95Ms = 0;
        qint64 encodeP50Ms = 0;
        qint64 encodeP95Ms = 0;
        double outputFps = 0.0;  // encoded frames over the last measurement window
        ClockCounters clock;

        qint64 droppedFrames() const
        {
            return droppedQueueFull + droppedQueueBytes + droppedQueueStale + encoderRejected;
        }
    };

    static constexpr int kTimingSamples = 512;
    static constexpr qint64 kFpsWindowNs = 2000000000;  // 2 s

    void reset(const QString& backend, quint64 generation);

    void frameOffered();
    void frameQueued(int depth, qint64 queuedBytes);
    void frameDropped(FrameDrop reason);
    // A frame left the queue and the encoder took it.
    void frameEncoded(qint64 queueAgeNs, qint64 encodeNs, qint64 nowNs);
    void audioQueued();
    void audioDropped(qint64 bytes);
    void setClockCounters(const ClockCounters& counters);

    Snapshot snapshot(qint64 nowNs) const;

    // Plain-text summary for the user to copy; no media or identifying data.
    static QString formatDiagnostics(const Snapshot& snapshot);

private:
    static qint64 percentileMs(QVector<qint64> samplesNs, int percent);

    mutable QMutex m_mutex;
    Snapshot m_counts;
    QVector<qint64> m_queueAgesNs;
    QVector<qint64> m_encodeNs;
    QVector<qint64> m_encodedAtNs;  // recent encode times for the output FPS
    int m_nextTiming = 0;
};

/**
 * Decides when the user should hear about sustained frame loss.
 *
 * Fed once per measurement window with how many frames were offered and
 * dropped in it. Warns after kBadWindows consecutive windows over the drop
 * ratio, at most once per kMinWarningIntervalMs, and reports recovery after
 * kGoodWindows clean windows. A single bad window (a hiccup) says nothing.
 */
class HealthWarningPolicy
{
public:
    enum class Action { None, Warn, Recovered };

    static constexpr double kDropRatio = 0.1;
    static constexpr int kBadWindows = 3;
    static constexpr int kGoodWindows = 5;
    static constexpr qint64 kMinWarningIntervalMs = 30000;

    Action evaluate(qint64 offeredInWindow, qint64 droppedInWindow, qint64 nowMs);
    bool isWarning() const { return m_warning; }
    void reset();

private:
    int m_badStreak = 0;
    int m_goodStreak = 0;
    bool m_warning = false;
    qint64 m_lastWarningMs = -1;
};

} // namespace SnapTray

#endif // RECORDINGHEALTH_H
