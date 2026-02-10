#include "scrollcapture/AutoScroller.h"

#include <QCursor>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_MAC
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

AutoScroller::~AutoScroller()
{
    restoreCursor();
}

bool AutoScroller::scroll(const Request& request, QString* errorMessage)
{
    if (!request.contentRectGlobal.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid content rectangle for scrolling.");
        }
        return false;
    }

    const QPoint targetPoint = request.contentRectGlobal.center();
    const int notches = std::max(1, request.stepPx / 120);
    const int sign = (request.direction == ScrollDirection::Down) ? -1 : 1;

#ifdef Q_OS_WIN
    auto sendWheelToWindow = [&](HWND hwnd, int wheelDelta) -> bool {
        if (!hwnd || !IsWindow(hwnd)) {
            return false;
        }

        HWND targetWindow = hwnd;

        POINT clientPoint{targetPoint.x(), targetPoint.y()};
        if (ScreenToClient(hwnd, &clientPoint)) {
            HWND child = ChildWindowFromPointEx(
                hwnd,
                clientPoint,
                CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
            if (child && child != hwnd && IsWindow(child)) {
                targetWindow = child;
            }
        }

        const int delta = wheelDelta;
        const WPARAM wParam = MAKEWPARAM(0, static_cast<WORD>(delta));
        const LPARAM lParam = MAKELPARAM(targetPoint.x(), targetPoint.y());
        SendMessageW(targetWindow, WM_MOUSEWHEEL, wParam, lParam);
        return true;
    };

    const int wheelDelta = sign * WHEEL_DELTA * notches;
    HWND hwnd = nullptr;
    if (request.nativeHandle != 0) {
        hwnd = reinterpret_cast<HWND>(request.nativeHandle);
    }
    if ((!hwnd || !IsWindow(hwnd))) {
        POINT point{targetPoint.x(), targetPoint.y()};
        hwnd = WindowFromPoint(point);
    }

    if (request.preferNoWarp && sendWheelToWindow(hwnd, wheelDelta)) {
        return true;
    }

    if (!request.allowCursorWarpFallback) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to inject wheel event to target window.");
        }
        return false;
    }

    if (!m_cursorMoved) {
        m_originalCursorPos = QCursor::pos();
        m_cursorMoved = true;
    }
    if (!SetCursorPos(targetPoint.x(), targetPoint.y())) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to move cursor to content area.");
        }
        return false;
    }

    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(wheelDelta);
    const UINT sent = SendInput(1, &input, sizeof(INPUT));
    if (sent != 1) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to inject wheel input.");
        }
        return false;
    }
    return true;
#elif defined(Q_OS_MAC)
    auto createScrollEvent = [&]() -> CGEventRef {
        return CGEventCreateScrollWheelEvent(
            nullptr,
            kCGScrollEventUnitPixel,
            1,
            sign * request.stepPx
        );
    };

    auto pidForWindowId = [](CGWindowID windowId) -> pid_t {
        CFArrayRef windows = CGWindowListCopyWindowInfo(
            kCGWindowListOptionIncludingWindow,
            windowId);
        if (!windows) {
            return 0;
        }

        pid_t pid = 0;
        if (CFArrayGetCount(windows) > 0) {
            auto* info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, 0));
            if (info) {
                CFNumberRef pidRef = static_cast<CFNumberRef>(
                    CFDictionaryGetValue(info, kCGWindowOwnerPID));
                if (pidRef) {
                    CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
                }
            }
        }
        CFRelease(windows);
        return pid;
    };

    if (request.preferNoWarp) {
        if (request.nativeHandle != 0) {
            const CGWindowID windowId = static_cast<CGWindowID>(request.nativeHandle);
            const pid_t pid = pidForWindowId(windowId);
            if (pid > 0) {
                CGEventRef event = createScrollEvent();
                if (event) {
                    CGEventSetLocation(event, CGPointMake(targetPoint.x(), targetPoint.y()));
                    CGEventPostToPid(pid, event);
                    CFRelease(event);
                    return true;
                }
            }
        }

        CGEventRef event = createScrollEvent();
        if (event) {
            CGEventSetLocation(event, CGPointMake(targetPoint.x(), targetPoint.y()));
            CGEventPost(kCGHIDEventTap, event);
            CFRelease(event);
            return true;
        }
    }

    if (!request.allowCursorWarpFallback) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to deliver scroll event to target window.");
        }
        return false;
    }

    if (!m_cursorMoved) {
        m_originalCursorPos = QCursor::pos();
        m_cursorMoved = true;
    }
    CGWarpMouseCursorPosition(CGPointMake(targetPoint.x(), targetPoint.y()));

    CGEventRef event = createScrollEvent();
    if (!event) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create scroll event.");
        }
        return false;
    }

    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
    return true;
#else
    Q_UNUSED(targetPoint);
    Q_UNUSED(notches);
    Q_UNUSED(sign);
    if (errorMessage) {
        *errorMessage = QStringLiteral("AutoScroller is unsupported on this platform.");
    }
    return false;
#endif
}

void AutoScroller::restoreCursor()
{
    if (!m_cursorMoved) {
        return;
    }

#ifdef Q_OS_WIN
    SetCursorPos(m_originalCursorPos.x(), m_originalCursorPos.y());
#elif defined(Q_OS_MAC)
    CGWarpMouseCursorPosition(CGPointMake(m_originalCursorPos.x(), m_originalCursorPos.y()));
#endif

    m_cursorMoved = false;
}
