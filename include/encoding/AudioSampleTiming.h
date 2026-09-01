#pragma once

#include <QtGlobal>

#include <limits>

namespace SnapTray::Audio {

struct ScaledSampleRange {
    qint64 start = 0;
    qint64 duration = 0;
    bool valid = false;
};

namespace detail {

constexpr bool scaleAudioFrameFloor(qint64 frame,
                                    int sampleRate,
                                    qint64 unitsPerSecond,
                                    qint64& scaled)
{
    if (frame < 0 || sampleRate <= 0 || unitsPerSecond <= 0) {
        return false;
    }

    const qint64 seconds = frame / sampleRate;
    const qint64 remainder = frame % sampleRate;
    constexpr qint64 maximum = std::numeric_limits<qint64>::max();
    if (seconds > maximum / unitsPerSecond) {
        return false;
    }

    const qint64 whole = seconds * unitsPerSecond;
    const qint64 unitsPerFrame = unitsPerSecond / sampleRate;
    const qint64 remainingUnits = unitsPerSecond % sampleRate;
    const qint64 fraction = remainder * unitsPerFrame
        + remainder * remainingUnits / sampleRate;
    if (fraction > maximum - whole) {
        return false;
    }

    scaled = whole + fraction;
    return true;
}

} // namespace detail

constexpr ScaledSampleRange scaleAudioSampleRange(qint64 startFrame,
                                                  qint64 frameCount,
                                                  int sampleRate,
                                                  qint64 unitsPerSecond)
{
    if (startFrame < 0 || frameCount < 0
        || frameCount > std::numeric_limits<qint64>::max() - startFrame) {
        return {};
    }

    qint64 start = 0;
    qint64 end = 0;
    if (!detail::scaleAudioFrameFloor(
            startFrame, sampleRate, unitsPerSecond, start)
        || !detail::scaleAudioFrameFloor(
            startFrame + frameCount, sampleRate, unitsPerSecond, end)) {
        return {};
    }
    return {start, end - start, true};
}

} // namespace SnapTray::Audio
