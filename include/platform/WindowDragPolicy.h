#pragma once

#include <QtGlobal>

namespace SnapTray {

// Zero keeps immediate movement on platforms that do not need pacing.
inline constexpr int windowDragUpdateIntervalMs()
{
#ifdef Q_OS_LINUX
    // Bound X11 geometry requests from high-rate pointer input. Keep the latest
    // position instead of queuing every sample, using an 8 ms timer interval.
    return 8;
#else
    return 0;
#endif
}

} // namespace SnapTray
