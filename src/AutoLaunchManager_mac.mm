#include "AutoLaunchManager.h"

#include "settings/AutoLaunchSyncPolicy.h"
#include "settings/AutoLaunchSettingsManager.h"

#import <ServiceManagement/ServiceManagement.h>
#include <QDebug>

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

bool AutoLaunchManager::syncWithPreference()
{
    auto& settingsManager = AutoLaunchSettingsManager::instance();
    const std::optional<bool> preferredEnabled = settingsManager.loadPreferredEnabled();
    const bool currentlyEnabled = isEnabled();
    const SnapTray::AutoLaunchSyncState state = currentlyEnabled
        ? SnapTray::AutoLaunchSyncState::EnabledCurrentCanonical
        : SnapTray::AutoLaunchSyncState::Disabled;
    const SnapTray::AutoLaunchSyncPlan plan =
        SnapTray::buildAutoLaunchStartupSyncPlan(preferredEnabled, state);

    if (plan.shouldApplyChange && !setEnabled(plan.targetEnabled)) {
        return isEnabled();
    }

    return isEnabled();
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
            // SMAppService requires a signed app bundle; expected to fail in
            // debug builds running from the build directory.
            qWarning("AutoLaunchManager: Failed to %s auto-launch: %s",
                     enabled ? "enable" : "disable",
                     error.localizedDescription.UTF8String);
        }

        return success;
    } else {
        // For older macOS versions, return false
        // Full implementation would require LSSharedFileList APIs
        return false;
    }
}
