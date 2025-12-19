#include "AutoLaunchManager.h"
#import <ServiceManagement/ServiceManagement.h>

bool AutoLaunchManager::isEnabled()
{
    if (@available(macOS 13.0, *)) {
        SMAppService *service = [SMAppService mainAppService];
        return service.status == SMAppServiceStatusEnabled;
    } else {
        // For older macOS versions, return false as a safe default
        // Full implementation would require LSSharedFileList APIs
        return false;
    }
}

bool AutoLaunchManager::setEnabled(bool enabled)
{
    if (@available(macOS 13.0, *)) {
        SMAppService *service = [SMAppService mainAppService];
        NSError *error = nil;
        BOOL success;

        if (enabled) {
            success = [service registerAndReturnError:&error];
        } else {
            success = [service unregisterAndReturnError:&error];
        }

        if (!success && error) {
            NSLog(@"AutoLaunchManager: Failed to %@ auto-launch: %@",
                  enabled ? @"enable" : @"disable",
                  error.localizedDescription);
        }

        return success;
    } else {
        // For older macOS versions, return false
        // Full implementation would require LSSharedFileList APIs
        return false;
    }
}
