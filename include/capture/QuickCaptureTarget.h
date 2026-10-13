#ifndef QUICKCAPTURETARGET_H
#define QUICKCAPTURETARGET_H

#include "WindowDetector.h"

#include <QRect>
#include <QString>

#include <optional>
#include <vector>

namespace snaptray::capture {

enum class QuickCaptureTarget {
    FullScreen,
    ActiveWindow
};

struct QuickCaptureRegion {
    QRect globalRect;       // Logical global coordinates, clipped to the screen
    QString windowTitle;
    QString ownerApp;
    bool isWindow = false;  // False when the capture covers the whole screen
};

// Returns the front-most application window or dialog that is visible on the
// screen. The detector cache is ordered front to back and already excludes
// SnapTray's own windows.
std::optional<DetectedElement> findFrontmostWindow(
    const std::vector<DetectedElement>& windows, const QRect& screenGeometry);

// Resolves the capture rectangle. Active-window requests fall back to the full
// screen when no usable window is available.
QuickCaptureRegion resolveQuickCaptureRegion(
    QuickCaptureTarget target,
    const QRect& screenGeometry,
    const std::optional<DetectedElement>& window);

} // namespace snaptray::capture

#endif // QUICKCAPTURETARGET_H
