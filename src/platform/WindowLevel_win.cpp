// Copyright (c) 2025 Victor Fu. All rights reserved.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "WindowLevel.h"

#ifdef Q_OS_WIN
#include <Windows.h>
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
        // WDA_EXCLUDEFROMCAPTURE = 0x00000011, available on Windows 10 version 2004+
        // This makes the window invisible to screen capture APIs (including DXGI)
        constexpr DWORD WDA_EXCLUDEFROMCAPTURE_VALUE = 0x00000011;
        DWORD affinity = excluded ? WDA_EXCLUDEFROMCAPTURE_VALUE : WDA_NONE;
        if (!SetWindowDisplayAffinity(hwnd, affinity)) {
            // May fail on older Windows versions - this is expected
            // The window will still be visible in captures on older systems
        }
    }
#else
    Q_UNUSED(widget)
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
