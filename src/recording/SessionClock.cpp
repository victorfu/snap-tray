#include "recording/SessionClock.h"

#include <QMutexLocker>

namespace SnapTray {

namespace {
constexpr qint64 kNsPerMs = 1000000;
}

void SessionClock::start(qint64 epochNs)
{
    QMutexLocker lock(&m_mutex);
    m_started = true;
    m_stopped = false;
    m_epochNs = epochNs;
    m_pauses.clear();
    m_lastAcceptedNs = -1;
    m_stats = {};
}

void SessionClock::pause(qint64 atNs)
{
    QMutexLocker lock(&m_mutex);
    if (!m_started || m_stopped || (!m_pauses.isEmpty() && m_pauses.last().endNs < 0)) {
        return;
    }
    m_pauses.append({qMax(atNs, m_epochNs), -1});
}

void SessionClock::resume(qint64 atNs)
{
    QMutexLocker lock(&m_mutex);
    if (m_pauses.isEmpty() || m_pauses.last().endNs >= 0) {
        return;
    }
    m_pauses.last().endNs = qMax(atNs, m_pauses.last().startNs);
}

void SessionClock::stop()
{
    QMutexLocker lock(&m_mutex);
    m_stopped = true;
}

void SessionClock::reset()
{
    QMutexLocker lock(&m_mutex);
    m_started = false;
    m_stopped = false;
    m_epochNs = 0;
    m_pauses.clear();
    m_lastAcceptedNs = -1;
    m_stats = {};
}

bool SessionClock::isStarted() const
{
    QMutexLocker lock(&m_mutex);
    return m_started && !m_stopped;
}

bool SessionClock::isPaused() const
{
    QMutexLocker lock(&m_mutex);
    return !m_pauses.isEmpty() && m_pauses.last().endNs < 0;
}

qint64 SessionClock::recordingTimeLocked(qint64 sourceTimeNs, bool* insidePause) const
{
    qint64 pausedNs = 0;
    *insidePause = false;
    for (const PauseSpan& span : m_pauses) {
        if (sourceTimeNs < span.startNs) {
            break;
        }
        if (span.endNs < 0 || sourceTimeNs < span.endNs) {
            *insidePause = true;
            pausedNs += sourceTimeNs - span.startNs;
            break;
        }
        pausedNs += span.endNs - span.startNs;
    }
    return sourceTimeNs - m_epochNs - pausedNs;
}

SessionClock::Admission SessionClock::admit(qint64 sourceTimeNs, qint64* recordingTimeNs)
{
    QMutexLocker lock(&m_mutex);
    if (!m_started) {
        ++m_stats.beforeStart;
        return Admission::BeforeStart;
    }
    if (m_stopped) {
        return Admission::Stopped;
    }
    if (sourceTimeNs < m_epochNs) {
        ++m_stats.beforeStart;
        return Admission::BeforeStart;
    }
    bool insidePause = false;
    const qint64 time = recordingTimeLocked(sourceTimeNs, &insidePause);
    if (insidePause) {
        ++m_stats.paused;
        return Admission::Paused;
    }
    if (time == m_lastAcceptedNs) {
        ++m_stats.duplicates;
        return Admission::Duplicate;
    }
    if (time < m_lastAcceptedNs) {
        ++m_stats.backwards;
        return Admission::Backward;
    }
    m_lastAcceptedNs = time;
    ++m_stats.accepted;
    if (recordingTimeNs) {
        *recordingTimeNs = time;
    }
    return Admission::Accepted;
}

qint64 SessionClock::recordingTimeAt(qint64 nowNs) const
{
    QMutexLocker lock(&m_mutex);
    if (!m_started || nowNs < m_epochNs) {
        return 0;
    }
    bool insidePause = false;
    return recordingTimeLocked(nowNs, &insidePause);
}

SessionClock::Stats SessionClock::stats() const
{
    QMutexLocker lock(&m_mutex);
    return m_stats;
}

qint64 SessionClock::toEncoderMs(qint64 recordingTimeNs)
{
    return recordingTimeNs <= 0 ? 0 : (recordingTimeNs + kNsPerMs / 2) / kNsPerMs;
}

} // namespace SnapTray
