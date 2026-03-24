#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"

RegionCaptureSettingsManager& RegionCaptureSettingsManager::instance()
{
    static RegionCaptureSettingsManager instance;
    return instance;
}

bool RegionCaptureSettingsManager::isMagnifierEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyShowMagnifier, kDefaultMagnifierEnabled).toBool();
}

void RegionCaptureSettingsManager::setMagnifierEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyShowMagnifier, enabled);
}

bool RegionCaptureSettingsManager::isShortcutHintsEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyShowShortcutHints, kDefaultShortcutHintsEnabled).toBool();
}

void RegionCaptureSettingsManager::setShortcutHintsEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyShowShortcutHints, enabled);
}
