// Copyright (c) 2026 Victor Fu. All rights reserved.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef WINDOWLEVEL_H
#define WINDOWLEVEL_H

#include <QCursor>
#include <QWindow>
#include <QWidget>

// Sets the window level above the menu bar (macOS) or does nothing (Windows)
void raiseWindowAboveMenuBar(QWidget *widget);

// Sets click-through mode for a window (mouse events pass through to windows underneath)
void setWindowClickThrough(QWidget *widget, bool enabled);

// Sets window to float above other windows without stealing focus
// On macOS: Uses NSPanel with non-activating style
// On Windows: Uses WS_EX_NOACTIVATE
void setWindowFloatingWithoutFocus(QWidget *widget);

// Excludes a window from screen capture
// On Windows: Uses SetWindowDisplayAffinity with WDA_EXCLUDEFROMCAPTURE (Windows 10 2004+)
// On macOS: Uses NSWindow.sharingType = NSWindowSharingNone
//   - Works with QScreen::grabWindow() and legacy CGWindowList APIs (macOS 10.5+)
//   - Does NOT affect ScreenCaptureKit on macOS 15+ (use SCContentFilter's
//     excludingWindows parameter for SCK exclusion)
void setWindowExcludedFromCapture(QWidget *widget, bool excluded);
void setWindowExcludedFromCapture(QWindow *window, bool excluded);

// Sets window to be visible on all virtual desktops/workspaces
// On macOS: Uses NSWindowCollectionBehaviorCanJoinAllSpaces
// On Windows: Uses WS_EX_TOOLWINDOW style
void setWindowVisibleOnAllWorkspaces(QWidget *widget, bool enabled);

// Prevents a native window from hiding automatically when the app deactivates.
// On macOS: Sets NSWindow.hidesOnDeactivate = NO after the native window exists.
// On Windows: No-op.
void preventWindowHideOnDeactivate(QWidget *widget);

// Reasserts a Qt cursor at the native platform layer.
// On macOS: maps system shapes or uploads custom pixmap cursors.
// On Windows: currently no-op (policy is still unified at the Qt layer).
void forceNativeCursor(const QCursor& cursor, QWidget *widget = nullptr);

// Raises window above overlay windows and their floating toolbars.
// On macOS: Sets NSWindow level to kCGScreenSaverWindowLevel + 2
// On Windows: No-op (Qt::WindowStaysOnTopHint is sufficient)
void raiseWindowAboveOverlays(QWidget *widget);

// Raises a transient QWindow above its parent widget's native window level.
// On macOS: Sets NSWindow level to max(NSFloatingWindowLevel, parent + 1)
// On Windows: No-op (Qt::WindowStaysOnTopHint is sufficient)
void raiseTransientWindowAboveParent(QWindow *window, QWidget *parentWidget);

// Reapplies native frameless-tool-window styles to an overlay QWindow.
// On Windows: strips caption/control-box bits that can resurface after owner/transient changes.
// On macOS: no-op.
void reinforceFramelessToolWindow(QWindow *window);

// Hides the native title-bar icon for standard top-level QWindows.
// On Windows: removes the caption icon while preserving the normal title bar/buttons.
// On macOS: no-op.
void hideNativeWindowTitleBarIcon(QWindow *window);

// Hides the native title-bar icon for standard top-level QWidgets.
// Delegates to the backing QWindow after the native handle exists.
void hideNativeWindowTitleBarIcon(QWidget *widget);

#endif // WINDOWLEVEL_H
