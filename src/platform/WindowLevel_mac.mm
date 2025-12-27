// Copyright (c) 2025 Victor Fu. All rights reserved.
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "WindowLevel.h"

#import <Cocoa/Cocoa.h>

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
