#include "settings/FileSettingsManager.h"
#include "settings/Settings.h"
#include <QStandardPaths>

FileSettingsManager& FileSettingsManager::instance()
{
    static FileSettingsManager instance;
    return instance;
}

QString FileSettingsManager::defaultScreenshotPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
}

QString FileSettingsManager::defaultRecordingPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

QString FileSettingsManager::loadScreenshotPath() const
{
    auto settings = SnapTray::getSettings();
    QString path = settings.value(kSettingsKeyScreenshotPath, defaultScreenshotPath()).toString();
    return path.isEmpty() ? defaultScreenshotPath() : path;
}

void FileSettingsManager::saveScreenshotPath(const QString& path)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyScreenshotPath, path);
}

QString FileSettingsManager::loadRecordingPath() const
{
    auto settings = SnapTray::getSettings();
    QString path = settings.value(kSettingsKeyRecordingPath, defaultRecordingPath()).toString();
    return path.isEmpty() ? defaultRecordingPath() : path;
}

void FileSettingsManager::saveRecordingPath(const QString& path)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyRecordingPath, path);
}

QString FileSettingsManager::loadFilenamePrefix() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyFilenamePrefix, defaultFilenamePrefix()).toString();
}

void FileSettingsManager::saveFilenamePrefix(const QString& prefix)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyFilenamePrefix, prefix);
}

QString FileSettingsManager::loadDateFormat() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyDateFormat, defaultDateFormat()).toString();
}

void FileSettingsManager::saveDateFormat(const QString& format)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyDateFormat, format);
}

bool FileSettingsManager::loadAutoSaveScreenshots() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyAutoSaveScreenshots, defaultAutoSaveScreenshots()).toBool();
}

void FileSettingsManager::saveAutoSaveScreenshots(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyAutoSaveScreenshots, enabled);
}

bool FileSettingsManager::loadAutoSaveRecordings() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyAutoSaveRecordings, defaultAutoSaveRecordings()).toBool();
}

void FileSettingsManager::saveAutoSaveRecordings(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyAutoSaveRecordings, enabled);
}
