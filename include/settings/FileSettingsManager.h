#ifndef FILESETTINGSMANAGER_H
#define FILESETTINGSMANAGER_H

#include <QString>

/**
 * @brief Singleton class for managing file save settings.
 *
 * This class provides centralized access to file-related settings
 * such as save paths, filename prefix, and date format.
 */
class FileSettingsManager
{
public:
    static FileSettingsManager& instance();

    // Screenshot path settings
    QString loadScreenshotPath() const;
    void saveScreenshotPath(const QString& path);

    // Recording path settings
    QString loadRecordingPath() const;
    void saveRecordingPath(const QString& path);

    // Filename prefix
    QString loadFilenamePrefix() const;
    void saveFilenamePrefix(const QString& prefix);

    // Date format (for filename)
    QString loadDateFormat() const;
    void saveDateFormat(const QString& format);

    // Filename template
    QString loadFilenameTemplate() const;
    void saveFilenameTemplate(const QString& templ);

    // Auto-save settings
    bool loadAutoSaveScreenshots() const;
    void saveAutoSaveScreenshots(bool enabled);
    bool loadAutoSaveRecordings() const;
    void saveAutoSaveRecordings(bool enabled);

    // Manual screenshot save location settings
    bool loadUseLastScreenshotSaveLocation() const;
    void saveUseLastScreenshotSaveLocation(bool enabled);
    QString resolveManualScreenshotSaveDirectory(bool useLastSaveLocation) const;
    void rememberManualScreenshotSaveDirectory(const QString& savedFilePath);

    // Default values
    static QString defaultScreenshotPath();
    static QString defaultRecordingPath();
    static QString defaultFilenamePrefix() { return QString(); }
    static QString defaultDateFormat() { return QStringLiteral("yyyyMMdd_HHmmss"); }
    static QString defaultFilenameTemplate();
    static bool defaultAutoSaveScreenshots() { return true; }
    static bool defaultAutoSaveRecordings() { return true; }
    static bool defaultUseLastScreenshotSaveLocation() { return false; }

private:
    FileSettingsManager() = default;
    FileSettingsManager(const FileSettingsManager&) = delete;
    FileSettingsManager& operator=(const FileSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyScreenshotPath = "files/screenshotPath";
    static constexpr const char* kSettingsKeyRecordingPath = "files/recordingPath";
    static constexpr const char* kSettingsKeyFilenamePrefix = "files/filenamePrefix";
    static constexpr const char* kSettingsKeyDateFormat = "files/dateFormat";
    static constexpr const char* kSettingsKeyFilenameTemplate = "files/filenameTemplate";
    static constexpr const char* kSettingsKeyAutoSaveScreenshots = "files/autoSaveScreenshots";
    static constexpr const char* kSettingsKeyAutoSaveRecordings = "files/autoSaveRecordings";
    static constexpr const char* kSettingsKeyUseLastScreenshotSaveLocation =
        "files/useLastScreenshotSaveLocation";
    static constexpr const char* kSettingsKeyLastScreenshotSaveDirectory =
        "files/lastScreenshotSaveDirectory";
};

#endif // FILESETTINGSMANAGER_H
