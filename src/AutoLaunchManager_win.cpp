#include "AutoLaunchManager.h"

#include "settings/AutoLaunchSyncPolicy.h"
#include "settings/AutoLaunchSettingsManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

static const char* RUN_KEY = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char* APP_NAME = "SnapTray";

namespace {

QString currentExecutablePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString canonicalStartupCommand()
{
    return QStringLiteral("\"%1\"").arg(currentExecutablePath());
}

QString startupEntryValue()
{
    QSettings settings(RUN_KEY, QSettings::NativeFormat);
    return settings.value(APP_NAME).toString().trimmed();
}

bool startupEntryExists()
{
    QSettings settings(RUN_KEY, QSettings::NativeFormat);
    return settings.contains(APP_NAME);
}

SnapTray::AutoLaunchSyncState startupEntryState()
{
    return SnapTray::classifyWindowsAutoLaunchEntry(
        startupEntryExists(),
        startupEntryValue(),
        currentExecutablePath());
}

}

bool AutoLaunchManager::isEnabled()
{
    const SnapTray::AutoLaunchSyncState state = startupEntryState();
    return state == SnapTray::AutoLaunchSyncState::EnabledCurrentCanonical
        || state == SnapTray::AutoLaunchSyncState::EnabledCurrentLegacy;
}

bool AutoLaunchManager::setEnabled(bool enabled)
{
    QSettings settings(RUN_KEY, QSettings::NativeFormat);

    if (enabled) {
        settings.setValue(APP_NAME, canonicalStartupCommand());
    } else {
        settings.remove(APP_NAME);
    }

    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool AutoLaunchManager::syncWithPreference()
{
    auto& settingsManager = AutoLaunchSettingsManager::instance();
    const std::optional<bool> preferredEnabled = settingsManager.loadPreferredEnabled();
    const SnapTray::AutoLaunchSyncPlan plan =
        SnapTray::buildAutoLaunchStartupSyncPlan(preferredEnabled, startupEntryState());

    if (plan.shouldApplyChange && !setEnabled(plan.targetEnabled)) {
        return isEnabled();
    }

    return isEnabled();
}
