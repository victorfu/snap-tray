#include "settings/MCPSettingsManager.h"
#include "settings/Settings.h"

MCPSettingsManager& MCPSettingsManager::instance()
{
    static MCPSettingsManager instance;
    return instance;
}

bool MCPSettingsManager::isEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyEnabled, kDefaultEnabled).toBool();
}

void MCPSettingsManager::setEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyEnabled, enabled);
}
