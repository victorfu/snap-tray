#include "scrolling/AutoScrollController.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QPoint>
#include <QCursor>
#include <windows.h>

namespace AutoScrollPlatform {

/**
 * @brief Injects a mouse wheel scroll event on Windows using SendInput
 *
 * This function positions the cursor within the capture region and sends
 * a wheel scroll event. The scroll delta is converted from pixels to
 * wheel delta units (WHEEL_DELTA = 120 per notch, typically ~40 pixels).
 *
 * @param deltaPixels Scroll amount in pixels
 * @param dir Scroll direction
 * @param region Capture region (cursor is moved to center)
 * @return true if scroll event was sent successfully
 */
bool injectScrollEvent(int deltaPixels, AutoScrollController::ScrollDirection dir, const QRect &region)
{
    if (region.isEmpty()) {
        qWarning() << "AutoScrollController: Cannot inject scroll - region is empty";
        return false;
    }

    // Calculate scroll wheel delta
    // WHEEL_DELTA (120) is the standard unit for one "notch" of scroll
    // Typically one notch scrolls about 3 lines (~40-60 pixels depending on settings)
    // We convert pixel delta to wheel units
    constexpr int PIXELS_PER_NOTCH = 40;
    int wheelDelta = (deltaPixels * WHEEL_DELTA) / PIXELS_PER_NOTCH;

    // Clamp to reasonable range (avoid extreme scrolls)
    wheelDelta = qBound(-WHEEL_DELTA * 20, wheelDelta, WHEEL_DELTA * 20);

    // Adjust sign based on direction
    // For vertical: negative delta = scroll down (content moves up)
    // For horizontal: negative delta = scroll right (content moves left)
    bool isVertical = (dir == AutoScrollController::ScrollDirection::Down ||
                       dir == AutoScrollController::ScrollDirection::Up);
    bool isPositiveDirection = (dir == AutoScrollController::ScrollDirection::Up ||
                                dir == AutoScrollController::ScrollDirection::Left);

    if (!isPositiveDirection) {
        wheelDelta = -wheelDelta;
    }

    // Move cursor to center of capture region
    QPoint center = region.center();

    // First, move the mouse to the target position
    INPUT moveInput = {};
    moveInput.type = INPUT_MOUSE;
    moveInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

    // Convert to absolute coordinates (0-65535 range)
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Handle virtual screen (multi-monitor)
    int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (virtualWidth > 0 && virtualHeight > 0) {
        // Use virtual screen coordinates for multi-monitor support
        moveInput.mi.dwFlags |= MOUSEEVENTF_VIRTUALDESK;
        moveInput.mi.dx = static_cast<LONG>(((center.x() - virtualLeft) * 65535) / virtualWidth);
        moveInput.mi.dy = static_cast<LONG>(((center.y() - virtualTop) * 65535) / virtualHeight);
    } else {
        // Single monitor fallback
        moveInput.mi.dx = static_cast<LONG>((center.x() * 65535) / screenWidth);
        moveInput.mi.dy = static_cast<LONG>((center.y() * 65535) / screenHeight);
    }

    // Send mouse move
    UINT moveResult = SendInput(1, &moveInput, sizeof(INPUT));
    if (moveResult != 1) {
        DWORD error = GetLastError();
        qWarning() << "AutoScrollController: Failed to move mouse, error:" << error;
        // Continue anyway - the cursor might already be in position
    }

    // Small delay to ensure cursor position is registered
    Sleep(5);

    // Now send the scroll event
    INPUT scrollInput = {};
    scrollInput.type = INPUT_MOUSE;

    if (isVertical) {
        scrollInput.mi.dwFlags = MOUSEEVENTF_WHEEL;
        scrollInput.mi.mouseData = static_cast<DWORD>(wheelDelta);
    } else {
        scrollInput.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        scrollInput.mi.mouseData = static_cast<DWORD>(wheelDelta);
    }

    UINT scrollResult = SendInput(1, &scrollInput, sizeof(INPUT));
    if (scrollResult != 1) {
        DWORD error = GetLastError();
        qWarning() << "AutoScrollController: Failed to send scroll event, error:" << error;
        return false;
    }

    qDebug() << "AutoScrollController: Injected scroll -"
             << "direction:" << static_cast<int>(dir)
             << "pixels:" << deltaPixels
             << "wheelDelta:" << wheelDelta
             << "at:" << center;

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
    // However, UIPI (User Interface Privilege Isolation) may block
    // sending input to higher-privilege processes
    return true;
}

void AutoScrollController::requestAccessibilityPermission()
{
    // No-op on Windows - permissions aren't required for normal applications
    // If the target window is elevated (running as admin), the user would
    // need to run SnapTray as admin as well
}

#endif // Q_OS_WIN
