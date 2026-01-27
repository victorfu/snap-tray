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

QString UpdateSettingsManager::skippedVersion() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeySkippedVersion).toString();
}

void UpdateSettingsManager::setSkippedVersion(const QString& version)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeySkippedVersion, version);
}

void UpdateSettingsManager::clearSkippedVersion()
{
    auto settings = SnapTray::getSettings();
    settings.remove(kSettingsKeySkippedVersion);
}

bool UpdateSettingsManager::includePrerelease() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyIncludePrerelease, kDefaultIncludePrerelease).toBool();
}

void UpdateSettingsManager::setIncludePrerelease(bool include)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyIncludePrerelease, include);
}

bool UpdateSettingsManager::shouldCheckForUpdates() const
{
    if (!isAutoCheckEnabled()) {
        return false;
    }

    QDateTime lastCheck = lastCheckTime();
    if (!lastCheck.isValid()) {
        // Never checked before
        return true;
    }

    int intervalHours = checkIntervalHours();
    QDateTime nextCheck = lastCheck.addSecs(intervalHours * 3600);
    return QDateTime::currentDateTime() >= nextCheck;
}
