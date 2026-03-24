#include "settings/AutoLaunchSyncPolicy.h"

#include <QDir>

namespace SnapTray {

namespace {

bool isCurrentEntryState(AutoLaunchSyncState state)
{
    return state == AutoLaunchSyncState::EnabledCurrentCanonical
        || state == AutoLaunchSyncState::EnabledCurrentLegacy;
}

}

AutoLaunchSyncState classifyWindowsAutoLaunchEntry(
    bool entryExists,
    const QString& entryValue,
    const QString& currentExecutablePath)
{
    if (!entryExists) {
        return AutoLaunchSyncState::Disabled;
    }

    const QString value = entryValue.trimmed();
    if (value.isEmpty()) {
        return AutoLaunchSyncState::EnabledOther;
    }

    const QString appPath = QDir::toNativeSeparators(currentExecutablePath);
    const QString canonicalCommand = QStringLiteral("\"%1\"").arg(appPath);
    if (value.compare(canonicalCommand, Qt::CaseInsensitive) == 0) {
        return AutoLaunchSyncState::EnabledCurrentCanonical;
    }

    if (value.startsWith('"')) {
        const int closingQuote = value.indexOf('"', 1);
        if (closingQuote <= 1) {
            return AutoLaunchSyncState::EnabledOther;
        }

        const QString configuredPath =
            QDir::toNativeSeparators(value.mid(1, closingQuote - 1));
        if (configuredPath.compare(appPath, Qt::CaseInsensitive) != 0) {
            return AutoLaunchSyncState::EnabledOther;
        }

        return AutoLaunchSyncState::EnabledCurrentLegacy;
    }

    if (!value.startsWith(appPath, Qt::CaseInsensitive)) {
        return AutoLaunchSyncState::EnabledOther;
    }

    if (value.size() == appPath.size()) {
        return AutoLaunchSyncState::EnabledCurrentLegacy;
    }

    return value.at(appPath.size()).isSpace()
        ? AutoLaunchSyncState::EnabledCurrentLegacy
        : AutoLaunchSyncState::EnabledOther;
}

AutoLaunchSyncPlan buildAutoLaunchStartupSyncPlan(
    std::optional<bool> preferredEnabled,
    AutoLaunchSyncState currentState)
{
    const bool currentlyEnabled = isCurrentEntryState(currentState);

    if (!preferredEnabled.has_value()) {
        AutoLaunchSyncPlan plan;
        plan.shouldApplyChange = currentState == AutoLaunchSyncState::EnabledCurrentLegacy;
        plan.targetEnabled = true;
        plan.effectiveEnabled = currentlyEnabled;
        return plan;
    }

    if (*preferredEnabled) {
        switch (currentState) {
        case AutoLaunchSyncState::Disabled:
            return AutoLaunchSyncPlan{true, true, false};
        case AutoLaunchSyncState::EnabledCurrentCanonical:
            return AutoLaunchSyncPlan{false, true, true};
        case AutoLaunchSyncState::EnabledCurrentLegacy:
            return AutoLaunchSyncPlan{true, true, true};
        case AutoLaunchSyncState::EnabledOther:
            return AutoLaunchSyncPlan{false, true, false};
        }
    }

    switch (currentState) {
    case AutoLaunchSyncState::Disabled:
        return AutoLaunchSyncPlan{false, false, false};
    case AutoLaunchSyncState::EnabledCurrentCanonical:
    case AutoLaunchSyncState::EnabledCurrentLegacy:
        return AutoLaunchSyncPlan{true, false, false};
    case AutoLaunchSyncState::EnabledOther:
        return AutoLaunchSyncPlan{false, false, false};
    }

    return AutoLaunchSyncPlan{};
}

} // namespace SnapTray
