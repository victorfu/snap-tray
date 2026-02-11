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

QString FileSettingsManager::defaultFilenameTemplate()
{
    return QStringLiteral("{prefix}_{type}_{yyyyMMdd_HHmmss}_{w}x{h}_{monitor}_{windowTitle}.{ext}");
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

QString FileSettingsManager::loadFilenameTemplate() const
{
    auto settings = SnapTray::getSettings();
    QString templ = settings.value(kSettingsKeyFilenameTemplate).toString().trimmed();
    if (!templ.isEmpty()) {
        return templ;
    }

    QString dateFormat = loadDateFormat().trimmed();
    if (dateFormat.isEmpty()) {
        dateFormat = defaultDateFormat();
    }

    // Migrate legacy prefix+date behavior into template mode.
    templ = QStringLiteral("{prefix}_{type}_{%1}.{ext}").arg(dateFormat);
    settings.setValue(kSettingsKeyFilenameTemplate, templ);
    return templ;
}

void FileSettingsManager::saveFilenameTemplate(const QString& templ)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyFilenameTemplate, templ.trimmed());
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
