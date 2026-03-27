#include "update/UpdateSettingsManager.h"
#include "settings/Settings.h"

UpdateSettingsManager& UpdateSettingsManager::instance()
{
    static UpdateSettingsManager instance;
    return instance;
}

bool UpdateSettingsManager::isAutoCheckEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyAutoCheck, kDefaultAutoCheck).toBool();
}

void UpdateSettingsManager::setAutoCheckEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyAutoCheck, enabled);
}

int UpdateSettingsManager::checkIntervalHours() const
{
    auto settings = SnapTray::getSettings();
    int hours = settings.value(kSettingsKeyCheckIntervalHours, kDefaultCheckIntervalHours).toInt();
    // Clamp to reasonable range (1 hour to 1 month)
    return qBound(1, hours, 720);
}

void UpdateSettingsManager::setCheckIntervalHours(int hours)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyCheckIntervalHours, qBound(1, hours, 720));
}

QDateTime UpdateSettingsManager::lastCheckTime() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyLastCheckTime).toDateTime();
}

void UpdateSettingsManager::setLastCheckTime(const QDateTime& time)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyLastCheckTime, time);
}
