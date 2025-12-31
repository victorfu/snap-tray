#include "scrolling/AutoScrollController.h"

#ifdef Q_OS_WIN

#include <QDebug>

// Windows implementation using SendInput (stub for now)
namespace AutoScrollPlatform {

bool injectScrollEvent(int deltaPixels, AutoScrollController::ScrollDirection dir, const QRect &region)
{
    Q_UNUSED(deltaPixels)
    Q_UNUSED(dir)
    Q_UNUSED(region)

    qWarning() << "AutoScrollController: Windows scroll injection not yet implemented";
    return false;

    // Future implementation:
    // INPUT input = {};
    // input.type = INPUT_MOUSE;
    // input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    // input.mi.mouseData = -deltaPixels; // Negative = scroll down
    // SendInput(1, &input, sizeof(INPUT));
}

} // namespace AutoScrollPlatform

bool AutoScrollController::isSupported()
{
    // Windows will support this once implemented
    return false;
}

bool AutoScrollController::hasAccessibilityPermission()
{
    // Windows doesn't require special permissions for SendInput
    return true;
}

void AutoScrollController::requestAccessibilityPermission()
{
    // No-op on Windows
}

#endif // Q_OS_WIN
