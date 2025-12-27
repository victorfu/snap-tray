// Copyright (c) 2025 Victor Fu. All rights reserved.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef WINDOWLEVEL_H
#define WINDOWLEVEL_H

#include <QWidget>

// Sets the window level above the menu bar (macOS) or does nothing (Windows)
void raiseWindowAboveMenuBar(QWidget *widget);

// Sets click-through mode for a window (mouse events pass through to windows underneath)
void setWindowClickThrough(QWidget *widget, bool enabled);

// Sets window to float above other windows without stealing focus
// On macOS: Uses NSPanel with non-activating style
// On Windows: Uses WS_EX_NOACTIVATE
void setWindowFloatingWithoutFocus(QWidget *widget);

#endif // WINDOWLEVEL_H
