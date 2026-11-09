#include "platform/MacTrayAccessibility.h"

#import <AppKit/AppKit.h>

namespace {

// The status item buttons Qt created for this application. A status item lives
// in its own NSStatusBarWindow, which AppKit lists among the app's windows.
NSArray<NSStatusBarButton*>* statusItemButtons()
{
    NSMutableArray<NSStatusBarButton*>* buttons = [NSMutableArray array];
    for (NSWindow* window in NSApp.windows) {
        NSMutableArray<NSView*>* pending = [NSMutableArray array];
        if (window.contentView) {
            [pending addObject:window.contentView];
        }
        while (pending.count > 0) {
            NSView* view = pending.lastObject;
            [pending removeLastObject];
            if ([view isKindOfClass:[NSStatusBarButton class]]) {
                [buttons addObject:static_cast<NSStatusBarButton*>(view)];
                continue;
            }
            [pending addObjectsFromArray:view.subviews];
        }
    }
    return buttons;
}

} // namespace

namespace SnapTray {

bool setMacTrayAccessibilityLabel(const QString& label)
{
    NSArray<NSStatusBarButton*>* buttons = statusItemButtons();
    for (NSStatusBarButton* button in buttons) {
        button.accessibilityLabel = label.toNSString();
    }
    return buttons.count > 0;
}

QString macTrayAccessibilityLabel()
{
    NSArray<NSStatusBarButton*>* buttons = statusItemButtons();
    return buttons.count > 0 ? QString::fromNSString(buttons.firstObject.accessibilityLabel) : QString();
}

} // namespace SnapTray
