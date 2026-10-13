// Copyright (c) 2026 Victor Fu. All rights reserved.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "WindowLevel.h"
#include "platform/CaptureExclusionPolicy.h"
#include <QDateTime>
#include <QDebug>

#ifdef Q_OS_WIN
#include <Windows.h>

namespace {
bool setWidgetCaptureAffinity(QWidget *widget, HWND hwnd, DWORD affinity, DWORD& error)
{
    if (SetWindowDisplayAffinity(hwnd, affinity)) {
        return true;
    }
    error = GetLastError();

    // Windows 10 can reject per-pixel-alpha QWidget surfaces with error 8.
    // Establish affinity without the layered surface, then restore Qt's style.
    // Keep this workaround specific to the reproduced failure; other errors
    // and QWindow renderers retain the normal diagnostic path.
    if (error != ERROR_NOT_ENOUGH_MEMORY ||
        !widget->testAttribute(Qt::WA_TranslucentBackground)) {
        return false;
    }
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_LAYERED)) {
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    if (!SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED)) {
        const DWORD styleError = GetLastError();
        if (styleError != ERROR_SUCCESS) {
            error = styleError;
            return false;
        }
    }

    const BOOL applied = SetWindowDisplayAffinity(hwnd, affinity);
    const DWORD affinityError = applied ? ERROR_SUCCESS : GetLastError();
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousStyle = SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    const DWORD restoreError = previousStyle ? ERROR_SUCCESS : GetLastError();

    // Toggling WS_EX_LAYERED invalidates its contents. Repaint the entire
    // surface, including when retrying affinity failed.
    widget->update();
    error = restoreError != ERROR_SUCCESS ? restoreError : affinityError;
    if (!applied || error != ERROR_SUCCESS) {
        return false;
    }

    qDebug() << "Window capture affinity applied after layered-window retry:"
             << widget->metaObject()->className();
    return true;
}

void logCaptureAffinityFailure(const QObject *object, HWND hwnd, bool excluded,
                               DWORD affinity, DWORD error, bool visible,
                               bool topLevel, Qt::WindowFlags flags, const QRect& geometry)
{
    // The caller saves GetLastError before any Qt or Win32 diagnostics run.
    LPWSTR messageBuffer = nullptr;
    const DWORD messageLength = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&messageBuffer), 0, nullptr);
    const QString errorMessage = messageLength
        ? QString::fromWCharArray(messageBuffer, static_cast<int>(messageLength)).simplified()
        : QStringLiteral("System error description unavailable");
    if (messageBuffer) {
        LocalFree(messageBuffer);
    }

    const auto hex = [](quint64 value) {
        return QStringLiteral("0x%1").arg(value, 0, 16);
    };
    const auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
    DWORD currentAffinity = 0;
    const BOOL affinityReadable = GetWindowDisplayAffinity(hwnd, &currentAffinity);
    const DWORD affinityReadError = affinityReadable ? ERROR_SUCCESS : GetLastError();
    const auto version = QOperatingSystemVersion::current();

    qWarning().nospace()
        << "Failed to set window capture affinity: " << error
        << " message=" << errorMessage
        << " timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        << " class=" << object->metaObject()->className()
        << " objectName=" << object->objectName()
        << " parentClass=" << (object->parent() ? object->parent()->metaObject()->className() : "<none>")
        << " hwnd=" << hex(reinterpret_cast<quintptr>(hwnd))
        << " excluded=" << excluded << " requestedAffinity=" << hex(affinity)
        << " affinityReadable=" << bool(affinityReadable)
        << " currentAffinity=" << (affinityReadable ? hex(currentAffinity) : QStringLiteral("unavailable"))
        << " affinityReadError=" << affinityReadError
        << " qtVisible=" << visible << " qtTopLevel=" << topLevel
        << " qtFlags=" << hex(static_cast<quint64>(flags.toInt()))
        << " geometry=" << geometry
        << " nativeValid=" << bool(IsWindow(hwnd))
        << " nativeVisible=" << bool(IsWindowVisible(hwnd))
        << " nativeTopLevel=" << (GetAncestor(hwnd, GA_ROOT) == hwnd)
        << " ownerHwnd=" << hex(reinterpret_cast<quintptr>(GetWindow(hwnd, GW_OWNER)))
        << " style=" << hex(style) << " exStyle=" << hex(exStyle)
        << " layered=" << bool(exStyle & WS_EX_LAYERED)
        << " transparent=" << bool(exStyle & WS_EX_TRANSPARENT)
        << " noRedirectionBitmap=" << bool(exStyle & WS_EX_NOREDIRECTIONBITMAP)
        << " windowProcessId=" << processId << " currentProcessId=" << GetCurrentProcessId()
        << " windowThreadId=" << threadId << " currentThreadId=" << GetCurrentThreadId()
        << " windowsVersion=" << version.majorVersion() << '.' << version.minorVersion()
        << '.' << version.microVersion() << " qtVersion=" << qVersion();
}
} // namespace
#endif

void raiseWindowAboveMenuBar(QWidget *)
{
    // Windows does not need special handling for window level
}

void setWindowClickThrough(QWidget *widget, bool enabled)
{
#ifdef Q_OS_WIN
    if (!widget) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd) {
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (enabled) {
            exStyle |= WS_EX_TRANSPARENT | WS_EX_LAYERED;
        } else {
            // Only remove WS_EX_TRANSPARENT, keep WS_EX_LAYERED for transparency support
            exStyle &= ~WS_EX_TRANSPARENT;
        }
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    }
#else
    Q_UNUSED(widget)
    Q_UNUSED(enabled)
#endif
}

void setWindowFloatingWithoutFocus(QWidget *widget)
{
#ifdef Q_OS_WIN
    if (!widget) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd) {
        // Add WS_EX_NOACTIVATE to prevent focus stealing
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_NOACTIVATE | WS_EX_TOPMOST;
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
#else
    Q_UNUSED(widget)
#endif
}

void setWindowExcludedFromCapture(QWidget *widget, bool excluded)
{
#ifdef Q_OS_WIN
    if (!widget) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd) {
        const DWORD affinity = SnapTray::windowsCaptureAffinity(excluded, QOperatingSystemVersion::current());
        DWORD error = ERROR_SUCCESS;
        if (!setWidgetCaptureAffinity(widget, hwnd, affinity, error)) {
            logCaptureAffinityFailure(widget, hwnd, excluded, affinity, error,
                                      widget->isVisible(), widget->isWindow(),
                                      widget->windowFlags(), widget->geometry());
        }
    }
#else
    Q_UNUSED(widget)
    Q_UNUSED(excluded)
#endif
}

void setWindowExcludedFromCapture(QWindow *window, bool excluded)
{
#ifdef Q_OS_WIN
    if (!window) {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd) {
        const DWORD affinity = SnapTray::windowsCaptureAffinity(excluded, QOperatingSystemVersion::current());
        if (!SetWindowDisplayAffinity(hwnd, affinity)) {
            const DWORD error = GetLastError();
            logCaptureAffinityFailure(window, hwnd, excluded, affinity, error,
                                      window->isVisible(), window->isTopLevel(),
                                      window->flags(), window->geometry());
        }
    }
#else
    Q_UNUSED(window)
    Q_UNUSED(excluded)
#endif
}

void setWindowVisibleOnAllWorkspaces(QWidget *widget, bool enabled)
{
#ifdef Q_OS_WIN
    if (!widget) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd) {
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (enabled) {
            exStyle |= WS_EX_TOOLWINDOW;
        } else {
            exStyle &= ~WS_EX_TOOLWINDOW;
        }
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    }
#else
    Q_UNUSED(widget)
    Q_UNUSED(enabled)
#endif
}

void preventWindowHideOnDeactivate(QWidget *widget)
{
    Q_UNUSED(widget)
}

void forceNativeCursor(const QCursor&, QWidget *)
{
    // Windows doesn't need separate native reassertion here.
}

void raiseWindowAboveOverlays(QWidget *)
{
    // Windows doesn't need special handling when dialogs are properly managed
    // (dialogs are destroyed on hide and recreated on show to avoid z-order corruption)
}

void raiseTransientWindowAboveParent(QWindow *, QWidget *)
{
    // Windows doesn't need special handling for transient popup z-order here.
}

void reinforceFramelessToolWindow(QWindow *window)
{
#ifdef Q_OS_WIN
    if (!window) {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    const LONG_PTR cleanedStyle = (style | WS_POPUP)
        & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    if (cleanedStyle != style) {
        SetWindowLongPtr(hwnd, GWL_STYLE, cleanedStyle);
    }

    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    LONG_PTR cleanedExStyle = (exStyle | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
    if (window->flags().testFlag(Qt::WindowDoesNotAcceptFocus)) {
        cleanedExStyle |= WS_EX_NOACTIVATE;
    }
    if (cleanedExStyle != exStyle) {
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, cleanedExStyle);
    }

    const HWND insertAfter = window->flags().testFlag(Qt::WindowStaysOnTopHint)
        ? HWND_TOPMOST
        : nullptr;
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOOWNERZORDER;
    if (!insertAfter) {
        flags |= SWP_NOZORDER;
    }
    SetWindowPos(hwnd, insertAfter, 0, 0, 0, 0, flags);
#else
    Q_UNUSED(window)
#endif
}

void hideNativeWindowTitleBarIcon(QWindow *window)
{
#ifdef Q_OS_WIN
    if (!window) {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }

    const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    const LONG_PTR cleanedExStyle = exStyle | WS_EX_DLGMODALFRAME;
    if (cleanedExStyle != exStyle) {
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, cleanedExStyle);
    }

    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, 0);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    Q_UNUSED(window)
#endif
}

void hideNativeWindowTitleBarIcon(QWidget *widget)
{
    if (!widget) {
        return;
    }

    QWindow *window = widget->window()->windowHandle();
    if (!window) {
        widget->winId();
        window = widget->window()->windowHandle();
    }

    hideNativeWindowTitleBarIcon(window);
}
