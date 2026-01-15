#include "scrolling/AutoScrollController.h"
#include "utils/CoordinateHelper.h"

#ifdef Q_OS_WIN

#include <Windows.h>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

namespace AutoScrollPlatform {

bool injectScrollEvent(int deltaPixels, AutoScrollController::ScrollDirection dir, const QRect &region)
{
    // The region is in Qt logical coordinates
    // Windows SendInput/SetCursorPos expect physical screen coordinates

    QPoint center = region.center();
    QScreen *screen = QGuiApplication::screenAt(center);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        qWarning() << "AutoScrollController: No screen available";
        return false;
    }

    qreal dpr = CoordinateHelper::getDevicePixelRatio(screen);

    // Convert center to physical coordinates for cursor positioning
    QPoint physicalCenter = CoordinateHelper::toPhysical(center, dpr);

    // Move cursor to center of capture region
    if (!SetCursorPos(physicalCenter.x(), physicalCenter.y())) {
        qWarning() << "AutoScrollController: SetCursorPos failed, error:" << GetLastError();
        return false;
    }

    // Prepare mouse input for scroll
    INPUT input = {};
    input.type = INPUT_MOUSE;

    // WHEEL_DELTA is 120 for one "notch"
    // Convert deltaPixels to wheel delta units
    int wheelDelta = (deltaPixels * WHEEL_DELTA) / 120;
    if (wheelDelta == 0) {
        wheelDelta = WHEEL_DELTA; // Minimum one notch
    }

    switch (dir) {
    case AutoScrollController::ScrollDirection::Down:
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        // Negative = scroll down (content moves up)
        input.mi.mouseData = static_cast<DWORD>(-wheelDelta);
        break;

    case AutoScrollController::ScrollDirection::Up:
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        // Positive = scroll up (content moves down)
        input.mi.mouseData = static_cast<DWORD>(wheelDelta);
        break;

    case AutoScrollController::ScrollDirection::Right:
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        // Positive = scroll right
        input.mi.mouseData = static_cast<DWORD>(wheelDelta);
        break;

    case AutoScrollController::ScrollDirection::Left:
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        // Negative = scroll left
        input.mi.mouseData = static_cast<DWORD>(-wheelDelta);
        break;
    }

    UINT sent = SendInput(1, &input, sizeof(INPUT));
    if (sent != 1) {
        qWarning() << "AutoScrollController: SendInput failed, error:" << GetLastError();
        return false;
    }

    qDebug() << "AutoScrollController: Injected scroll event"
             << "delta:" << deltaPixels
             << "direction:" << static_cast<int>(dir)
             << "at physical:" << physicalCenter.x() << "," << physicalCenter.y();

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
