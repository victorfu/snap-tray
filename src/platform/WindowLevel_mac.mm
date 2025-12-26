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
