#include "platform/MacDockPresence.h"

#import <AppKit/AppKit.h>

namespace {

int dockPresenceClaims = 0;

} // namespace

namespace SnapTray {

DockPresenceClaim::DockPresenceClaim()
{
    if (dockPresenceClaims++ == 0) {
        @autoreleasepool {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        }
    }
}

DockPresenceClaim::~DockPresenceClaim()
{
    if (--dockPresenceClaims == 0) {
        @autoreleasepool {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        }
    }
}

} // namespace SnapTray
