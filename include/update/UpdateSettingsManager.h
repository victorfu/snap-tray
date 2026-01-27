#ifndef UPDATESETTINGSMANAGER_H
#define UPDATESETTINGSMANAGER_H

#include <QDateTime>
#include <QString>

/**
 * @brief Singleton class for managing auto-update settings.
 *
 * This class provides centralized access to update-related settings
 * such as auto-check enabled, check interval, skipped versions, etc.
 */
class UpdateSettingsManager
{
public:
    static UpdateSettingsManager& instance();

    // Auto check settings
    bool isAutoCheckEnabled() const;
    void setAutoCheckEnabled(bool enabled);

    // Check interval (in hours)
    int checkIntervalHours() const;
    void setCheckIntervalHours(int hours);

    // Last check time
    QDateTime lastCheckTime() const;
    void setLastCheckTime(const QDateTime& time);

    // Skipped version (user chose to skip this version)
    QString skippedVersion() const;
    void setSkippedVersion(const QString& version);
    void clearSkippedVersion();

    // Pre-release channel
    bool includePrerelease() const;
    void setIncludePrerelease(bool include);

    // Check if it's time to check for updates
    bool shouldCheckForUpdates() const;

    // Default values
    static constexpr bool kDefaultAutoCheck = true;
    static constexpr int kDefaultCheckIntervalHours = 24;
    static constexpr bool kDefaultIncludePrerelease = false;

private:
    UpdateSettingsManager() = default;
    UpdateSettingsManager(const UpdateSettingsManager&) = delete;
    UpdateSettingsManager& operator=(const UpdateSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyAutoCheck = "update/autoCheck";
    static constexpr const char* kSettingsKeyCheckIntervalHours = "update/checkIntervalHours";
    static constexpr const char* kSettingsKeyLastCheckTime = "update/lastCheckTime";
    static constexpr const char* kSettingsKeySkippedVersion = "update/skippedVersion";
    static constexpr const char* kSettingsKeyIncludePrerelease = "update/includePrerelease";
};

#endif // UPDATESETTINGSMANAGER_H
