#pragma once
#include <QtGlobal>
#include <array>
#include <cmath>

namespace SnapTray {
// Translate a QPC presentation timestamp by its age, rather than converting
// a large uptime through floating point and losing fractional milliseconds.
inline qint64 desktopPresentationTimeNs(qint64 presented, qint64 counter, qint64 frequency, qint64 nowNs)
{
    if (presented <= 0 || frequency <= 0 || counter < presented) return nowNs;
    const long double age = static_cast<long double>(counter - presented) * 1000000000.0L / frequency;
    if (age >= nowNs) return 0;
    return nowNs - qint64(std::llround(age));
}
inline qint64 desktopRecoveryDelayNs(int failures)
{
    constexpr std::array<qint64, 4> seconds{{1, 2, 4, 8}};
    return seconds[size_t(qBound(0, failures, int(seconds.size()) - 1))] * 1000000000LL;
}
inline bool desktopImageChanged(qint64 presentTime, quint32 accumulatedFrames)
{
    return presentTime != 0 && accumulatedFrames != 0;
}
}
