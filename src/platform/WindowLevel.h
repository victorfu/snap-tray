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

// Sets or removes the system window shadow
// On macOS: Uses NSWindow setHasShadow:
// On Windows: No-op (Windows uses our custom-drawn shadow)
void setWindowShadow(QWidget *widget, bool enabled);

// Excludes a window from screen capture
// On Windows: Uses SetWindowDisplayAffinity with WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+)
// On macOS: No-op (use ScreenCaptureKit's excludingWindows instead)
void setWindowExcludedFromCapture(QWidget *widget, bool excluded);

// Sets window to be visible on all virtual desktops/workspaces
// On macOS: Uses NSWindowCollectionBehaviorCanJoinAllSpaces
// On Windows: Uses WS_EX_TOOLWINDOW style
void setWindowVisibleOnAllWorkspaces(QWidget *widget, bool enabled);

#endif // WINDOWLEVEL_H
