#include "scrolling/AutoScrollController.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <windows.h>

// Windows implementation using SendInput API
namespace AutoScrollPlatform {

bool injectScrollEvent(int deltaPixels, AutoScrollController::ScrollDirection dir, const QRect &region)
{
    // Calculate target point (center of capture region)
    QPoint center = region.center();

    // Get device pixel ratio for high-DPI displays
    QScreen *screen = QGuiApplication::primaryScreen();
    qreal devicePixelRatio = screen ? screen->devicePixelRatio() : 1.0;

    // Convert to screen coordinates (Windows uses physical pixels)
    int screenX = static_cast<int>(center.x() * devicePixelRatio);
    int screenY = static_cast<int>(center.y() * devicePixelRatio);

    // Move cursor to center of capture region
    // This ensures scroll events are received by the correct window
    if (!SetCursorPos(screenX, screenY)) {
        qWarning() << "AutoScrollController: Failed to set cursor position";
        return false;
    }

    // Prepare scroll input
    INPUT input = {};
    input.type = INPUT_MOUSE;

    // WHEEL_DELTA is the standard unit for mouse wheel scrolling (typically 120)
    // We convert pixel delta to wheel units
    int wheelUnits = (deltaPixels * WHEEL_DELTA) / 100;
    if (wheelUnits == 0) {
        wheelUnits = WHEEL_DELTA; // Minimum one notch
    }

    switch (dir) {
    case AutoScrollController::ScrollDirection::Down:
        // Scroll down = negative wheel delta (content moves up)
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(-wheelUnits);
        break;

    case AutoScrollController::ScrollDirection::Up:
        // Scroll up = positive wheel delta (content moves down)
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(wheelUnits);
        break;

    case AutoScrollController::ScrollDirection::Right:
        // Scroll right = positive horizontal wheel delta
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        input.mi.mouseData = static_cast<DWORD>(wheelUnits);
        break;

    case AutoScrollController::ScrollDirection::Left:
        // Scroll left = negative horizontal wheel delta
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        input.mi.mouseData = static_cast<DWORD>(-wheelUnits);
        break;
    }

    // Send the scroll input
    UINT result = SendInput(1, &input, sizeof(INPUT));
    if (result != 1) {
        qWarning() << "AutoScrollController: SendInput failed, error:" << GetLastError();
        return false;
    }

    qDebug() << "AutoScrollController: Injected scroll event"
             << "delta:" << deltaPixels
             << "wheelUnits:" << wheelUnits
             << "direction:" << static_cast<int>(dir)
             << "at:" << screenX << "," << screenY;

    return true;
}

} // namespace AutoScrollPlatform

bool AutoScrollController::isSupported()
{
    // Windows supports scroll injection via SendInput
    return true;
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
