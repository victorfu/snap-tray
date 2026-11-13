#pragma once

#include "recording/CapturedFrame.h"

namespace SnapTray {

// Apply WGC's emission ordering without changing the captured picture or its
// ownership. The caller serializes emission and remembers the returned time.
inline CapturedFrame wgcFrameForEmission(const CapturedFrame& frame, qint64 lastEmittedNs)
{
    CapturedFrame stamped = frame;
    constexpr qint64 kMinimumFrameAdvanceNs = 1;
    if (lastEmittedNs >= 0 && stamped.sourceTimeNs <= lastEmittedNs) {
        stamped.sourceTimeNs = lastEmittedNs + kMinimumFrameAdvanceNs;
    }
    return stamped;
}

} // namespace SnapTray
