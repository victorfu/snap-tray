#include "AutoLaunchManager.h"

#include "settings/AutoLaunchSyncPolicy.h"
#include "settings/AutoLaunchSettingsManager.h"

#import <ServiceManagement/ServiceManagement.h>
#include <QDebug>

#include <utility>

namespace {

AutoLaunchStatus currentAutoLaunchStatus()
{
    if (@available(macOS 13.0, *)) {
        SMAppService *service = [SMAppService mainAppService];
        switch (service.status) {
        case SMAppServiceStatusNotRegistered:
            return {AutoLaunchState::Disabled, {}};
        case SMAppServiceStatusEnabled:
            return {AutoLaunchState::Enabled, {}};
        case SMAppServiceStatusRequiresApproval:
            return {AutoLaunchState::DisabledByUser, {}};
        case SMAppServiceStatusNotFound:
            return {
                AutoLaunchState::Unavailable,
                QStringLiteral("The macOS login item could not be found.")
            };
        }

        return {
            AutoLaunchState::Unavailable,
            QStringLiteral("macOS returned an unknown login item status.")
        };
    }

    return {
        AutoLaunchState::Unavailable,
        QStringLiteral("Start on login requires macOS 13 or later.")
    };
}

} // namespace

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
    const AutoLaunchStatus currentStatus = currentAutoLaunchStatus();
    if (!currentStatus.canChange()) {
        return currentStatus.isEnabled();
    }

    const SnapTray::AutoLaunchSyncState state = currentStatus.isEnabled()
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

void AutoLaunchManager::queryStatus(StatusCallback callback)
{
    if (callback) {
        callback(currentAutoLaunchStatus());
    }
}

void AutoLaunchManager::setEnabledAsync(bool enabled, StatusCallback callback)
{
    if (!callback) {
        return;
    }

    AutoLaunchStatus status = currentAutoLaunchStatus();
    if (!status.canChange()) {
        callback(std::move(status));
        return;
    }

    if (!setEnabled(enabled)) {
        status = currentAutoLaunchStatus();
        status.errorMessage = QStringLiteral("Failed to update the macOS login item.");
        callback(std::move(status));
        return;
    }

    callback(currentAutoLaunchStatus());
}
