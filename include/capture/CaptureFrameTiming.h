#pragma once

#include <chrono>

namespace SnapTray {
// Zero denotes an invalid rate. Nanoseconds retain fractional milliseconds at
// rates such as 24/60 fps instead of rounding every worker tick down.
inline std::chrono::nanoseconds captureFrameInterval(int fps)
{
    if (fps <= 0) return std::chrono::nanoseconds::zero();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) / fps;
}
}
