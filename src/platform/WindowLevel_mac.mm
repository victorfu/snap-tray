// Copyright (c) 2025 Victor Fu. All rights reserved.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "WindowLevel.h"

#import <Cocoa/Cocoa.h>

namespace {
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
API_AVAILABLE(macos(15.0))
NSCursor* frameResizeCursor(NSCursorFrameResizePosition position)
{
    return [NSCursor frameResizeCursorFromPosition:position
                                     inDirections:NSCursorFrameResizeDirectionsAll];
}
#endif

NSCursor* nsCursorForQtCursor(const QCursor& cursor)
{
    switch (cursor.shape()) {
    case Qt::ArrowCursor:
        return [NSCursor arrowCursor];
    case Qt::CrossCursor:
        return [NSCursor crosshairCursor];
    case Qt::PointingHandCursor:
        return [NSCursor pointingHandCursor];
    case Qt::OpenHandCursor:
        return [NSCursor openHandCursor];
    case Qt::ClosedHandCursor:
        return [NSCursor closedHandCursor];
    case Qt::IBeamCursor:
        return [NSCursor IBeamCursor];
    case Qt::SizeAllCursor:
        return [NSCursor openHandCursor];
    case Qt::SizeHorCursor:
        return [NSCursor resizeLeftRightCursor];
    case Qt::SizeVerCursor:
        return [NSCursor resizeUpDownCursor];
    case Qt::SizeFDiagCursor:
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
        if (@available(macOS 15.0, *)) {
            return frameResizeCursor(NSCursorFrameResizePositionTopLeft);
        }
#endif
        break;
    case Qt::SizeBDiagCursor:
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
        if (@available(macOS 15.0, *)) {
            return frameResizeCursor(NSCursorFrameResizePositionTopRight);
        }
#endif
        break;
    default:
        break;
    }

    QPixmap pixmap = cursor.pixmap();
    if (pixmap.isNull()) {
        const QBitmap bitmap = cursor.bitmap();
        const QBitmap mask = cursor.mask();
        if (!bitmap.isNull() && !mask.isNull()) {
            const QPoint hotspot = cursor.hotSpot();
            const QCursor bitmapCursor(bitmap, mask, hotspot.x(), hotspot.y());
            pixmap = bitmapCursor.pixmap();
        }
    }

    if (pixmap.isNull()) {
        return nil;
    }

    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (image.isNull()) {
        return nil;
    }

    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nil
        pixelsWide:image.width()
        pixelsHigh:image.height()
        bitsPerSample:8
        samplesPerPixel:4
        hasAlpha:YES
        isPlanar:NO
        colorSpaceName:NSCalibratedRGBColorSpace
        bytesPerRow:static_cast<NSInteger>(image.bytesPerLine())
        bitsPerPixel:32];
    if (!rep) {
        return nil;
    }

    memcpy([rep bitmapData], image.constBits(), static_cast<size_t>(image.sizeInBytes()));

    const qreal dpr = pixmap.devicePixelRatio() > 0.0 ? pixmap.devicePixelRatio() : 1.0;
    NSImage* nsImage = [[NSImage alloc]
        initWithSize:NSMakeSize(image.width() / dpr, image.height() / dpr)];
    [nsImage addRepresentation:rep];

    const QPoint hotspot = cursor.hotSpot();
    return [[NSCursor alloc] initWithImage:nsImage
                                   hotSpot:NSMakePoint(hotspot.x(), hotspot.y())];
}
}  // namespace

void raiseWindowAboveMenuBar(QWidget *widget)
{
    if (!widget) {
        return;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    if (view) {
        NSWindow *window = [view window];
        [window setLevel:kCGScreenSaverWindowLevel]; // level 1000
    }
}

void setWindowClickThrough(QWidget *widget, bool enabled)
{
    if (!widget) {
        return;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    if (view) {
        NSWindow *window = [view window];
        [window setIgnoresMouseEvents:enabled];
    }
}

void setWindowFloatingWithoutFocus(QWidget *widget)
{
    if (!widget) {
        return;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    if (view) {
        NSWindow *window = [view window];
        NSInteger targetLevel = NSFloatingWindowLevel;
        if (auto *parentWidget = widget->parentWidget()) {
            NSView *parentView = reinterpret_cast<NSView *>(parentWidget->winId());
            if (parentView) {
                NSWindow *parentWindow = [parentView window];
                if (parentWindow) {
                    targetLevel = [parentWindow level];
                }
            }
        }
        // Match parent window level so it stays visible above the pin window.
        [window setLevel:targetLevel];
        // Set collection behavior to ignore keyboard focus cycle
        NSWindowCollectionBehavior behavior = [window collectionBehavior];
        behavior |= NSWindowCollectionBehaviorIgnoresCycle;
        behavior |= NSWindowCollectionBehaviorStationary;
        behavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
        [window setCollectionBehavior:behavior];
        // Keep it visible when app is not active
        [window setHidesOnDeactivate:NO];
    }
}

void setWindowExcludedFromCapture(QWidget *widget, bool excluded)
{
    if (!widget) {
        return;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    if (view) {
        NSWindow *window = [view window];
        if (window) {
            // NSWindowSharingNone excludes window from CGWindowListCreateImage
            // and other CoreGraphics capture APIs (used by Qt's grabWindow).
            // This works on macOS 10.5+.
            // Note: On macOS 15+, ScreenCaptureKit ignores this flag, but SCK has
            // its own exclusion mechanism via SCContentFilter's excludingWindows.
            [window setSharingType:excluded ? NSWindowSharingNone : NSWindowSharingReadOnly];
        }
    }
}

void setWindowVisibleOnAllWorkspaces(QWidget *widget, bool enabled)
{
    if (!widget) {
        return;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    if (view) {
        NSWindow *window = [view window];
        NSWindowCollectionBehavior behavior = [window collectionBehavior];
        if (enabled) {
            behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces;
            behavior &= ~NSWindowCollectionBehaviorMoveToActiveSpace;
        } else {
            behavior &= ~NSWindowCollectionBehaviorCanJoinAllSpaces;
        }
        [window setCollectionBehavior:behavior];
    }
}

void preventWindowHideOnDeactivate(QWidget *widget)
{
    if (!widget) {
        return;
    }

    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    if (!view) {
        return;
    }

    NSWindow *window = [view window];
    if (!window) {
        return;
    }

    [window setHidesOnDeactivate:NO];
}

void forceNativeCursor(const QCursor& cursor, QWidget *)
{
    if (NSCursor* nsCursor = nsCursorForQtCursor(cursor)) {
        [nsCursor set];
    }
}

void raiseWindowAboveOverlays(QWidget *widget)
{
    if (!widget) {
        return;
    }
    NSView *view = reinterpret_cast<NSView *>(widget->winId());
    if (view) {
        NSWindow *window = [view window];
        // RegionSelector / ScreenCanvas live at kCGScreenSaverWindowLevel and
        // their floating toolbar overlays use +1. Native transient popups
        // (QMenu, dialogs) must sit above both layers to remain usable.
        [window setLevel:kCGScreenSaverWindowLevel + 2];
    }
}

void raiseTransientWindowAboveParent(QWindow *window, QWidget *parentWidget)
{
    if (!window) {
        return;
    }

    NSView *view = reinterpret_cast<NSView *>(window->winId());
    if (!view) {
        return;
    }

    NSWindow *nsWindow = [view window];
    if (!nsWindow) {
        return;
    }

    NSInteger targetLevel = NSFloatingWindowLevel;
    if (parentWidget) {
        NSView *parentView = reinterpret_cast<NSView *>(parentWidget->winId());
        if (parentView) {
            NSWindow *parentWindow = [parentView window];
            if (parentWindow) {
                targetLevel = qMax<NSInteger>(targetLevel, [parentWindow level] + 1);
            }
        }
    }

    [nsWindow setLevel:targetLevel];
    [nsWindow setHidesOnDeactivate:NO];
}
