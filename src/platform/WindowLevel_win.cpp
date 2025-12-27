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
