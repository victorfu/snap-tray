#include "settings/AutoLaunchSettingsManager.h"

#include "settings/Settings.h"

AutoLaunchSettingsManager& AutoLaunchSettingsManager::instance()
{
    static AutoLaunchSettingsManager instance;
    return instance;
}

std::optional<bool> AutoLaunchSettingsManager::loadPreferredEnabled() const
{
    auto settings = SnapTray::getSettings();
    if (!settings.contains(kKeyPreferredEnabled)) {
        return std::nullopt;
    }

    return settings.value(kKeyPreferredEnabled).toBool();
}

void AutoLaunchSettingsManager::savePreferredEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyPreferredEnabled, enabled);
    settings.sync();
}
