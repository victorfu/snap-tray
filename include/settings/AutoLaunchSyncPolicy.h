#pragma once

#include <optional>

#include <QString>

namespace SnapTray {

enum class AutoLaunchSyncState {
    Disabled,
    EnabledCurrentCanonical,
    EnabledCurrentLegacy,
    EnabledOther
};

struct AutoLaunchSyncPlan
{
    bool shouldApplyChange = false;
    bool targetEnabled = false;
    bool effectiveEnabled = false;
};

AutoLaunchSyncState classifyWindowsAutoLaunchEntry(
    bool entryExists,
    const QString& entryValue,
    const QString& currentExecutablePath);

AutoLaunchSyncPlan buildAutoLaunchStartupSyncPlan(
    std::optional<bool> preferredEnabled,
    AutoLaunchSyncState currentState);

} // namespace SnapTray
