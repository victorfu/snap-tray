#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class UpdateChecker;

namespace SnapTray {

class SettingsBackend : public QObject
{
    Q_OBJECT

    // ──── General ────
    Q_PROPERTY(bool startOnLogin READ startOnLogin WRITE setStartOnLogin NOTIFY startOnLoginChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(int appTheme READ appTheme WRITE setAppTheme NOTIFY appThemeChanged)
    Q_PROPERTY(bool cliInstalled READ cliInstalled NOTIFY cliInstalledChanged)
    Q_PROPERTY(bool isMacOS READ isMacOS CONSTANT)

#ifdef Q_OS_MAC
    // ──── macOS Permissions ────
    Q_PROPERTY(bool hasScreenRecordingPermission READ hasScreenRecordingPermission NOTIFY permissionsChanged)
    Q_PROPERTY(bool hasAccessibilityPermission READ hasAccessibilityPermission NOTIFY permissionsChanged)
#endif

    // ──── Advanced ────
    Q_PROPERTY(bool shortcutHintsEnabled READ shortcutHintsEnabled WRITE setShortcutHintsEnabled NOTIFY shortcutHintsEnabledChanged)
    Q_PROPERTY(bool isMcpBuild READ isMcpBuild CONSTANT)
#ifdef SNAPTRAY_ENABLE_MCP
    Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY mcpEnabledChanged)
#endif
    Q_PROPERTY(int blurIntensity READ blurIntensity WRITE setBlurIntensity NOTIFY blurIntensityChanged)
    Q_PROPERTY(int blurType READ blurType WRITE setBlurType NOTIFY blurTypeChanged)
    Q_PROPERTY(int pinDefaultOpacity READ pinDefaultOpacity WRITE setPinDefaultOpacity NOTIFY pinDefaultOpacityChanged)
    Q_PROPERTY(int pinOpacityStep READ pinOpacityStep WRITE setPinOpacityStep NOTIFY pinOpacityStepChanged)
    Q_PROPERTY(int pinZoomStep READ pinZoomStep WRITE setPinZoomStep NOTIFY pinZoomStepChanged)
    Q_PROPERTY(int pinMaxCacheFiles READ pinMaxCacheFiles WRITE setPinMaxCacheFiles NOTIFY pinMaxCacheFilesChanged)

    // ──── Watermark ────
    Q_PROPERTY(bool watermarkEnabled READ watermarkEnabled WRITE setWatermarkEnabled NOTIFY watermarkEnabledChanged)
    Q_PROPERTY(bool watermarkApplyToRecording READ watermarkApplyToRecording WRITE setWatermarkApplyToRecording NOTIFY watermarkApplyToRecordingChanged)
    Q_PROPERTY(QString watermarkImagePath READ watermarkImagePath WRITE setWatermarkImagePath NOTIFY watermarkImagePathChanged)
    Q_PROPERTY(int watermarkScale READ watermarkScale WRITE setWatermarkScale NOTIFY watermarkScaleChanged)
    Q_PROPERTY(int watermarkOpacity READ watermarkOpacity WRITE setWatermarkOpacity NOTIFY watermarkOpacityChanged)
    Q_PROPERTY(int watermarkMargin READ watermarkMargin WRITE setWatermarkMargin NOTIFY watermarkMarginChanged)
    Q_PROPERTY(int watermarkPosition READ watermarkPosition WRITE setWatermarkPosition NOTIFY watermarkPositionChanged)
    Q_PROPERTY(QString watermarkPreviewUrl READ watermarkPreviewUrl NOTIFY watermarkImagePathChanged)

    // ──── Recording ────
    Q_PROPERTY(int recordingFrameRate READ recordingFrameRate WRITE setRecordingFrameRate NOTIFY recordingFrameRateChanged)
    Q_PROPERTY(int recordingOutputFormat READ recordingOutputFormat WRITE setRecordingOutputFormat NOTIFY recordingOutputFormatChanged)
    Q_PROPERTY(int recordingQuality READ recordingQuality WRITE setRecordingQuality NOTIFY recordingQualityChanged)
    Q_PROPERTY(bool recordingShowPreview READ recordingShowPreview WRITE setRecordingShowPreview NOTIFY recordingShowPreviewChanged)
    Q_PROPERTY(bool recordingAudioEnabled READ recordingAudioEnabled WRITE setRecordingAudioEnabled NOTIFY recordingAudioEnabledChanged)
    Q_PROPERTY(int recordingAudioSource READ recordingAudioSource WRITE setRecordingAudioSource NOTIFY recordingAudioSourceChanged)
    Q_PROPERTY(QString recordingAudioDevice READ recordingAudioDevice WRITE setRecordingAudioDevice NOTIFY recordingAudioDeviceChanged)
    Q_PROPERTY(bool countdownEnabled READ countdownEnabled WRITE setCountdownEnabled NOTIFY countdownEnabledChanged)
    Q_PROPERTY(int countdownSeconds READ countdownSeconds WRITE setCountdownSeconds NOTIFY countdownSecondsChanged)

    // ──── Files ────
    Q_PROPERTY(QString screenshotPath READ screenshotPath WRITE setScreenshotPath NOTIFY screenshotPathChanged)
    Q_PROPERTY(QString recordingPath READ recordingPath WRITE setRecordingPath NOTIFY recordingPathChanged)
    Q_PROPERTY(QString filenameTemplate READ filenameTemplate WRITE setFilenameTemplate NOTIFY filenameTemplateChanged)
    Q_PROPERTY(QString filenamePreview READ filenamePreview NOTIFY filenameTemplateChanged)
    Q_PROPERTY(bool autoSaveScreenshots READ autoSaveScreenshots WRITE setAutoSaveScreenshots NOTIFY autoSaveScreenshotsChanged)
    Q_PROPERTY(bool autoSaveRecordings READ autoSaveRecordings WRITE setAutoSaveRecordings NOTIFY autoSaveRecordingsChanged)

    // ──── Updates ────
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(int checkFrequencyHours READ checkFrequencyHours WRITE setCheckFrequencyHours NOTIFY checkFrequencyHoursChanged)
    Q_PROPERTY(QString lastCheckedText READ lastCheckedText NOTIFY lastCheckedTextChanged)
    Q_PROPERTY(bool isCheckingForUpdates READ isCheckingForUpdates NOTIFY isCheckingForUpdatesChanged)

    // ──── About ────
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    explicit SettingsBackend(QObject* parent = nullptr);
    ~SettingsBackend() override;

    // ──── General ────
    bool startOnLogin() const;
    void setStartOnLogin(bool v);
    QString language() const;
    void setLanguage(const QString& v);
    QVariantList availableLanguages() const;
    int appTheme() const;
    void setAppTheme(int v);
    bool cliInstalled() const;
    bool isMacOS() const;

#ifdef Q_OS_MAC
    // ──── macOS Permissions ────
    bool hasScreenRecordingPermission() const;
    bool hasAccessibilityPermission() const;
#endif

    // ──── Advanced ────
    bool shortcutHintsEnabled() const;
    void setShortcutHintsEnabled(bool v);
    bool isMcpBuild() const;
#ifdef SNAPTRAY_ENABLE_MCP
    bool mcpEnabled() const;
    void setMcpEnabled(bool v);
#endif
    int blurIntensity() const;
    void setBlurIntensity(int v);
    int blurType() const;
    void setBlurType(int v);
    int pinDefaultOpacity() const;
    void setPinDefaultOpacity(int v);
    int pinOpacityStep() const;
    void setPinOpacityStep(int v);
    int pinZoomStep() const;
    void setPinZoomStep(int v);
    int pinMaxCacheFiles() const;
    void setPinMaxCacheFiles(int v);

    // ──── Watermark ────
    bool watermarkEnabled() const;
    void setWatermarkEnabled(bool v);
    bool watermarkApplyToRecording() const;
    void setWatermarkApplyToRecording(bool v);
    QString watermarkImagePath() const;
    void setWatermarkImagePath(const QString& v);
    int watermarkScale() const;
    void setWatermarkScale(int v);
    int watermarkOpacity() const;
    void setWatermarkOpacity(int v);
    int watermarkMargin() const;
    void setWatermarkMargin(int v);
    int watermarkPosition() const;
    void setWatermarkPosition(int v);
    QString watermarkPreviewUrl() const;

    // ──── Recording ────
    int recordingFrameRate() const;
    void setRecordingFrameRate(int v);
    int recordingOutputFormat() const;
    void setRecordingOutputFormat(int v);
    int recordingQuality() const;
    void setRecordingQuality(int v);
    bool recordingShowPreview() const;
    void setRecordingShowPreview(bool v);
    bool recordingAudioEnabled() const;
    void setRecordingAudioEnabled(bool v);
    int recordingAudioSource() const;
    void setRecordingAudioSource(int v);
    QString recordingAudioDevice() const;
    void setRecordingAudioDevice(const QString& v);
    bool countdownEnabled() const;
    void setCountdownEnabled(bool v);
    int countdownSeconds() const;
    void setCountdownSeconds(int v);

    // ──── Files ────
    QString screenshotPath() const;
    void setScreenshotPath(const QString& v);
    QString recordingPath() const;
    void setRecordingPath(const QString& v);
    QString filenameTemplate() const;
    void setFilenameTemplate(const QString& v);
    QString filenamePreview() const;
    bool autoSaveScreenshots() const;
    void setAutoSaveScreenshots(bool v);
    bool autoSaveRecordings() const;
    void setAutoSaveRecordings(bool v);

    // ──── Updates ────
    QString currentVersion() const;
    bool autoCheckUpdates() const;
    void setAutoCheckUpdates(bool v);
    int checkFrequencyHours() const;
    void setCheckFrequencyHours(int v);
    QString lastCheckedText() const;
    bool isCheckingForUpdates() const;

    // ──── About ────
    QString appName() const;
    QString appVersion() const;

    // ──── Q_INVOKABLE methods ────
    Q_INVOKABLE void save();
    Q_INVOKABLE void cancel();

    Q_INVOKABLE void installCLI();
    Q_INVOKABLE void uninstallCLI();

#ifdef Q_OS_MAC
    Q_INVOKABLE void openScreenRecordingSettings();
    Q_INVOKABLE void openAccessibilitySettings();
    Q_INVOKABLE void refreshPermissions();
#endif

    Q_INVOKABLE void browseScreenshotPath();
    Q_INVOKABLE void browseRecordingPath();
    Q_INVOKABLE void browseWatermarkImage();

    Q_INVOKABLE void checkForUpdates();

    Q_INVOKABLE void loadOcrLanguages();
    Q_INVOKABLE QVariantList ocrAvailableLanguages() const;
    Q_INVOKABLE QVariantList ocrSelectedLanguages() const;
    Q_INVOKABLE void addOcrLanguage(const QString& code);
    Q_INVOKABLE void removeOcrLanguage(const QString& code);
    Q_INVOKABLE void moveOcrLanguage(int from, int to);
    Q_INVOKABLE int ocrBehavior() const;
    Q_INVOKABLE void setOcrBehavior(int behavior);

    Q_INVOKABLE QVariantList hotkeyCategories() const;
    Q_INVOKABLE QVariantList hotkeysForCategory(int category) const;
    Q_INVOKABLE void editHotkey(int action);
    Q_INVOKABLE void clearHotkey(int action);
    Q_INVOKABLE void resetHotkey(int action);
    Q_INVOKABLE void restoreAllHotkeys();

    Q_INVOKABLE QVariantList audioDevices() const;

signals:
    void settingsSaved(bool languageChangeRequiresRestart);
    void settingsCancelled();

    // General
    void startOnLoginChanged();
    void languageChanged();
    void appThemeChanged();
    void cliInstalledChanged();

#ifdef Q_OS_MAC
    void permissionsChanged();
#endif

    // Advanced
    void shortcutHintsEnabledChanged();
    void mcpEnabledChanged(bool enabled);
    void blurIntensityChanged();
    void blurTypeChanged();
    void pinDefaultOpacityChanged();
    void pinOpacityStepChanged();
    void pinZoomStepChanged();
    void pinMaxCacheFilesChanged();

    // Watermark
    void watermarkEnabledChanged();
    void watermarkApplyToRecordingChanged();
    void watermarkImagePathChanged();
    void watermarkScaleChanged();
    void watermarkOpacityChanged();
    void watermarkMarginChanged();
    void watermarkPositionChanged();

    // Recording
    void recordingFrameRateChanged();
    void recordingOutputFormatChanged();
    void recordingQualityChanged();
    void recordingShowPreviewChanged();
    void recordingAudioEnabledChanged();
    void recordingAudioSourceChanged();
    void recordingAudioDeviceChanged();
    void countdownEnabledChanged();
    void countdownSecondsChanged();

    // Files
    void screenshotPathChanged();
    void recordingPathChanged();
    void filenameTemplateChanged();
    void autoSaveScreenshotsChanged();
    void autoSaveRecordingsChanged();

    // Updates
    void autoCheckUpdatesChanged();
    void checkFrequencyHoursChanged();
    void lastCheckedTextChanged();
    void isCheckingForUpdatesChanged();

    // Update check results
    void updateAvailable(const QString& version, const QString& notes);
    void noUpdateAvailable();
    void updateCheckFailed(const QString& error);

    // Hotkeys
    void hotkeysChanged();

    // OCR
    void ocrLanguagesChanged(const QStringList& languages);

private:
    void loadAllSettings();
    QString computeFilenamePreview() const;
    void normalizeRecordingAudioSettings();

    // ──── Buffered values ────

    // General
    bool m_startOnLogin = false;
    QString m_language;
    QString m_originalLanguage;
    int m_appTheme = 0;
    int m_originalAppTheme = 0;
    bool m_cliInstalled = false;

#ifdef Q_OS_MAC
    bool m_hasScreenRecordingPermission = false;
    bool m_hasAccessibilityPermission = false;
#endif

    // Advanced
    bool m_shortcutHintsEnabled = true;
#ifdef SNAPTRAY_ENABLE_MCP
    bool m_mcpEnabled = false;
#endif
    int m_blurIntensity = 50;
    int m_blurType = 0;
    int m_pinDefaultOpacity = 100;
    int m_pinOpacityStep = 5;
    int m_pinZoomStep = 5;
    int m_pinMaxCacheFiles = 20;

    // Watermark
    bool m_watermarkEnabled = false;
    bool m_watermarkApplyToRecording = false;
    QString m_watermarkImagePath;
    int m_watermarkScale = 100;
    int m_watermarkOpacity = 50;
    int m_watermarkMargin = 12;
    int m_watermarkPosition = 3;

    // Recording
    int m_recordingFrameRate = 30;
    int m_recordingOutputFormat = 0;
    int m_recordingQuality = 55;
    bool m_recordingShowPreview = true;
    bool m_recordingAudioEnabled = false;
    int m_recordingAudioSource = 0;
    QString m_recordingAudioDevice;
    bool m_countdownEnabled = true;
    int m_countdownSeconds = 3;

    // Files
    QString m_screenshotPath;
    QString m_recordingPath;
    QString m_filenameTemplate;
    bool m_autoSaveScreenshots = true;
    bool m_autoSaveRecordings = true;

    // Updates
    bool m_autoCheckUpdates = true;
    int m_checkFrequencyHours = 24;
    bool m_isCheckingForUpdates = false;

    // OCR (lazy-loaded)
    QStringList m_ocrSelectedLanguages;
    int m_ocrBehavior = 0;
    bool m_ocrLoaded = false;

    UpdateChecker* m_updateChecker = nullptr;
};

} // namespace SnapTray
