#include "recording/RecordingHealth.h"

#include <QMutexLocker>
#include <QStringList>

#include <algorithm>

namespace SnapTray {

namespace {
constexpr qint64 kNsPerMs = 1000000;
constexpr double kNsPerSecond = 1e9;
constexpr int kMedianPercent = 50;
constexpr int kTailPercent = 95;
}

void RecordingHealth::reset(const QString& backend, quint64 generation)
{
    QMutexLocker lock(&m_mutex);
    m_counts = {};
    m_counts.backend = backend;
    m_counts.generation = generation;
    m_queueAgesNs.clear();
    m_encodeNs.clear();
    m_encodedAtNs.clear();
    m_nextTiming = 0;
}

void RecordingHealth::frameOffered()
{
    QMutexLocker lock(&m_mutex);
    ++m_counts.offered;
}

void RecordingHealth::frameQueued(int depth, qint64 queuedBytes)
{
    QMutexLocker lock(&m_mutex);
    ++m_counts.queued;
    m_counts.peakQueueDepth = qMax(m_counts.peakQueueDepth, depth);
    m_counts.peakQueueBytes = qMax(m_counts.peakQueueBytes, queuedBytes);
}

void RecordingHealth::frameDropped(FrameDrop reason)
{
    QMutexLocker lock(&m_mutex);
    switch (reason) {
    case FrameDrop::QueueFull: ++m_counts.droppedQueueFull; break;
    case FrameDrop::QueueBytes: ++m_counts.droppedQueueBytes; break;
    case FrameDrop::QueueStale: ++m_counts.droppedQueueStale; break;
    case FrameDrop::EncoderRejected: ++m_counts.encoderRejected; break;
    }
}

void RecordingHealth::frameEncoded(qint64 queueAgeNs, qint64 encodeNs, qint64 nowNs)
{
    QMutexLocker lock(&m_mutex);
    ++m_counts.encoded;
    // Fixed-size rings: bounded memory however long the recording runs.
    if (m_queueAgesNs.size() < kTimingSamples) {
        m_queueAgesNs.append(queueAgeNs);
        m_encodeNs.append(encodeNs);
    } else {
        m_queueAgesNs[m_nextTiming] = queueAgeNs;
        m_encodeNs[m_nextTiming] = encodeNs;
        m_nextTiming = (m_nextTiming + 1) % kTimingSamples;
    }
    m_encodedAtNs.append(nowNs);
    while (!m_encodedAtNs.isEmpty() && nowNs - m_encodedAtNs.first() > kFpsWindowNs) {
        m_encodedAtNs.removeFirst();
    }
}

void RecordingHealth::audioQueued()
{
    QMutexLocker lock(&m_mutex);
    ++m_counts.audioQueued;
}

void RecordingHealth::audioDropped(qint64 bytes)
{
    QMutexLocker lock(&m_mutex);
    ++m_counts.audioDropped;
    m_counts.audioDroppedBytes += bytes;
}

void RecordingHealth::setClockCounters(const ClockCounters& counters)
{
    QMutexLocker lock(&m_mutex);
    m_counts.clock = counters;
}

qint64 RecordingHealth::percentileMs(QVector<qint64> samplesNs, int percent)
{
    if (samplesNs.isEmpty()) {
        return 0;
    }
    const int index = qMin<int>(samplesNs.size() - 1, (samplesNs.size() * percent) / 100);
    std::nth_element(samplesNs.begin(), samplesNs.begin() + index, samplesNs.end());
    return (samplesNs[index] + kNsPerMs / 2) / kNsPerMs;
}

RecordingHealth::Snapshot RecordingHealth::snapshot(qint64 nowNs) const
{
    QMutexLocker lock(&m_mutex);
    Snapshot result = m_counts;
    result.queueAgeP50Ms = percentileMs(m_queueAgesNs, kMedianPercent);
    result.queueAgeP95Ms = percentileMs(m_queueAgesNs, kTailPercent);
    result.encodeP50Ms = percentileMs(m_encodeNs, kMedianPercent);
    result.encodeP95Ms = percentileMs(m_encodeNs, kTailPercent);
    qint64 recent = 0;
    for (const qint64 at : m_encodedAtNs) {
        if (nowNs - at <= kFpsWindowNs) ++recent;
    }
    result.outputFps = recent * kNsPerSecond / kFpsWindowNs;
    return result;
}

QString RecordingHealth::formatDiagnostics(const Snapshot& s)
{
    QStringList lines;
    lines << QStringLiteral("SnapTray recording diagnostics");
    lines << QStringLiteral("capture backend: %1 (session %2)").arg(s.backend).arg(s.generation);
    lines << QStringLiteral("frames: offered %1, queued %2, encoded %3").arg(s.offered).arg(s.queued).arg(s.encoded);
    lines << QStringLiteral("dropped: queue full %1, over byte budget %2, stale queue %3, encoder rejected %4")
                 .arg(s.droppedQueueFull).arg(s.droppedQueueBytes).arg(s.droppedQueueStale).arg(s.encoderRejected);
    lines << QStringLiteral("not recorded by design: before start %1, paused %2, out of order %3, over frame rate %4")
                 .arg(s.clock.beforeStart).arg(s.clock.paused).arg(s.clock.outOfOrder).arg(s.clock.pacedOut);
    lines << QStringLiteral("cadence: late %1, stalls %2, longest gap %3 ms")
                 .arg(s.clock.late).arg(s.clock.stalls).arg(s.clock.longestGapMs);
    lines << QStringLiteral("queue: peak %1 frames, peak %2 MiB, wait p50 %3 ms, p95 %4 ms")
                 .arg(s.peakQueueDepth).arg(double(s.peakQueueBytes) / (1024 * 1024), 0, 'f', 1)
                 .arg(s.queueAgeP50Ms).arg(s.queueAgeP95Ms);
    lines << QStringLiteral("encode: p50 %1 ms, p95 %2 ms, output %3 fps (last 2 s)")
                 .arg(s.encodeP50Ms).arg(s.encodeP95Ms).arg(s.outputFps, 0, 'f', 1);
    lines << QStringLiteral("audio: queued %1 packets, dropped %2 packets (%3 bytes; kept as silence)")
                 .arg(s.audioQueued).arg(s.audioDropped).arg(s.audioDroppedBytes);
    return lines.join(QLatin1Char('\n'));
}

HealthWarningPolicy::Action HealthWarningPolicy::evaluate(qint64 offeredInWindow, qint64 droppedInWindow,
                                                          qint64 nowMs)
{
    const bool bad = offeredInWindow > 0 && double(droppedInWindow) / offeredInWindow > kDropRatio;
    if (bad) {
        ++m_badStreak;
        m_goodStreak = 0;
    } else {
        // A window without frames (static screen) is neither good nor bad.
        if (offeredInWindow > 0) ++m_goodStreak;
        m_badStreak = 0;
    }
    if (!m_warning && m_badStreak >= kBadWindows
        && (m_lastWarningMs < 0 || nowMs - m_lastWarningMs >= kMinWarningIntervalMs)) {
        m_warning = true;
        m_lastWarningMs = nowMs;
        return Action::Warn;
    }
    if (m_warning && m_goodStreak >= kGoodWindows) {
        m_warning = false;
        return Action::Recovered;
    }
    return Action::None;
}

void HealthWarningPolicy::reset()
{
    m_badStreak = 0;
    m_goodStreak = 0;
    m_warning = false;
    m_lastWarningMs = -1;
}

} // namespace SnapTray
