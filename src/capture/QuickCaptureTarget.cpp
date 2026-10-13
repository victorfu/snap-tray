#include "capture/QuickCaptureTarget.h"

namespace snaptray::capture {

namespace {

// Ignore slivers such as window edges peeking in from a neighbouring screen.
constexpr int kMinVisibleWindowSide = 8;

bool isCapturableElementType(ElementType type)
{
    return type == ElementType::Window || type == ElementType::Dialog;
}

QRect visibleBounds(const DetectedElement& element, const QRect& screenGeometry)
{
    return element.bounds.intersected(screenGeometry);
}

bool hasUsableVisibleArea(const QRect& visible)
{
    return visible.width() >= kMinVisibleWindowSide
        && visible.height() >= kMinVisibleWindowSide;
}

} // namespace

std::optional<DetectedElement> findFrontmostWindow(
    const std::vector<DetectedElement>& windows, const QRect& screenGeometry)
{
    for (const DetectedElement& element : windows) {
        if (!isCapturableElementType(element.elementType)) {
            continue;
        }
        if (hasUsableVisibleArea(visibleBounds(element, screenGeometry))) {
            return element;
        }
    }
    return std::nullopt;
}

QuickCaptureRegion resolveQuickCaptureRegion(
    QuickCaptureTarget target,
    const QRect& screenGeometry,
    const std::optional<DetectedElement>& window)
{
    QuickCaptureRegion region;
    region.globalRect = screenGeometry;

    if (target != QuickCaptureTarget::ActiveWindow || !window.has_value()) {
        return region;
    }

    const QRect visible = visibleBounds(*window, screenGeometry);
    if (!hasUsableVisibleArea(visible)) {
        return region;
    }

    region.globalRect = visible;
    region.windowTitle = window->windowTitle;
    region.ownerApp = window->ownerApp;
    region.isWindow = true;
    return region;
}

} // namespace snaptray::capture
