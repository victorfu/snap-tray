#include "settings/MCPSettingsManager.h"
#include "settings/Settings.h"

MCPSettingsManager& MCPSettingsManager::instance()
{
    static MCPSettingsManager instance;
    return instance;
}

bool MCPSettingsManager::isEnabled() const
{
#ifdef SNAPTRAY_ENABLE_MCP
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyEnabled, kDefaultEnabled).toBool();
#else
    return false;
#endif
}

void MCPSettingsManager::setEnabled(bool enabled)
{
#ifdef SNAPTRAY_ENABLE_MCP
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyEnabled, enabled);
#else
    (void)enabled;
#endif
}
