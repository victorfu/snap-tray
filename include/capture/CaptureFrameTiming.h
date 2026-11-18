#pragma once

#include <chrono>
#include <QtGlobal>

namespace SnapTray {
// Sparse refreshes preserve liveness and bound muxer latency without sending
// an unchanged screen through conversion/encoding at the requested frame rate.
constexpr qint64 kCaptureKeepAliveNs = 2000000000LL;
// Zero denotes an invalid rate. Nanoseconds retain fractional milliseconds at
// rates such as 24/60 fps instead of rounding every worker tick down.
inline std::chrono::nanoseconds captureFrameInterval(int fps)
{
    if (fps <= 0) return std::chrono::nanoseconds::zero();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) / fps;
}
// A final desktop update may be rejected by the downstream frame-rate cap.
// Replay the newest cached pixels once after a quiet three-quarter interval:
// beyond the pacer's 60% spacing, but before the next nominal frame boundary.
inline std::chrono::nanoseconds captureSettlingInterval(int fps)
{
    return captureFrameInterval(fps) * 3 / 4;
}
}
