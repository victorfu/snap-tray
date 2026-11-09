#include "platform/MacTrayActivationGuard.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <QDebug>

namespace {

using ClickCallback = void (*)(id, SEL);
using MenuCallback = void (*)(id, SEL, NSNotification*);
ClickCallback originalClick = nullptr;
MenuCallback originalMenuTracking = nullptr;

bool hasMouseButtonEvent()
{
    NSEvent* event = NSApp.currentEvent;
    if (!event) {
        return false;
    }
    const NSEventMask buttonEvents = NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp
        | NSEventMaskRightMouseDown | NSEventMaskRightMouseUp
        | NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp;
    return (NSEventMaskFromType(event.type) & buttonEvents) != 0;
}

void guardedClick(id receiver, SEL selector)
{
    if (hasMouseButtonEvent()) {
        originalClick(receiver, selector);
    }
}

void guardedMenuTracking(id receiver, SEL selector, NSNotification* notification)
{
    if (hasMouseButtonEvent()) {
        originalMenuTracking(receiver, selector, notification);
    }
}

} // namespace

namespace SnapTray {

bool installMacTrayActivationGuard()
{
    if (NSProcessInfo.processInfo.operatingSystemVersion.majorVersion < 27) {
        return true;
    }
    if (originalClick && originalMenuTracking) {
        return true;
    }

    // QTBUG-147449: macOS 27 can open status menus with a KitDefined event.
    // Qt 6.11.2 reads clickCount unconditionally in emitActivated(), which
    // throws for that event. Guard only Qt's two activation entry points;
    // never replace NSEvent behavior or swallow unrelated Cocoa exceptions.
    // SnapTray uses QAction triggers, not the tray's activated signal, so
    // skipping non-mouse activation leaves native menu tracking intact.
    // Remove this shim once our minimum Qt includes the upstream event guard.
    Class delegate = NSClassFromString(@"QStatusItemDelegate");
    Method click = class_getInstanceMethod(delegate, NSSelectorFromString(@"statusItemClicked"));
    Method tracking = class_getInstanceMethod(delegate, NSSelectorFromString(@"statusItemMenuBeganTracking:"));
    if (!click || !tracking
        || method_getNumberOfArguments(click) != 2
        || method_getNumberOfArguments(tracking) != 3) {
        qWarning() << "Cannot install Qt macOS tray activation guard: unexpected Cocoa delegate";
        return false;
    }

    originalClick = reinterpret_cast<ClickCallback>(method_getImplementation(click));
    originalMenuTracking = reinterpret_cast<MenuCallback>(method_getImplementation(tracking));
    method_setImplementation(click, reinterpret_cast<IMP>(guardedClick));
    method_setImplementation(tracking, reinterpret_cast<IMP>(guardedMenuTracking));
    return true;
}

} // namespace SnapTray
