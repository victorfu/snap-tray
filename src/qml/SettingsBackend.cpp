#include "qml/SettingsBackend.h"

#include "AutoLaunchManager.h"
#include "PlatformFeatures.h"
#include "capture/IAudioCaptureEngine.h"
#include "hotkey/HotkeyManager.h"
#include "hotkey/HotkeyTypes.h"
#include "settings/AutoBlurSettingsManager.h"
#include "settings/FileSettingsManager.h"
#include "settings/LanguageManager.h"
#include "settings/MCPSettingsManager.h"
#include "settings/OCRSettingsManager.h"
#include "settings/PinWindowSettingsManager.h"
#include "settings/RecordingSettingsManager.h"
#include "settings/RegionCaptureSettingsManager.h"
#include "settings/Settings.h"
#include "settings/WatermarkSettingsManager.h"
#include "OCRManager.h"
#include "ToolbarStyle.h"
#include "qml/ThemeManager.h"
#include "update/UpdateChecker.h"
#include "update/UpdateSettingsManager.h"
#include "utils/FilenameTemplateEngine.h"
#include "widgets/TypeHotkeyDialog.h"

#include <QFileDialog>
#include <QLocale>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace SnapTray {

SettingsBackend::SettingsBackend(QObject* parent)
    : QObject(parent)
{
    loadAllSettings();
}

SettingsBackend::~SettingsBackend() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Load all settings from managers into buffered members
// ─────────────────────────────────────────────────────────────────────────────

void SettingsBackend::loadAllSettings()
{
    // General
    m_startOnLogin = AutoLaunchManager::isEnabled();
    m_language = LanguageManager::instance().loadLanguage();
    m_originalLanguage = m_language;
    m_appTheme = static_cast<int>(ToolbarStyleConfig::loadStyle());
    m_originalAppTheme = m_appTheme;
    m_cliInstalled = PlatformFeatures::instance().isCLIInstalled();

#ifdef Q_OS_MAC
    m_hasScreenRecordingPermission = PlatformFeatures::hasScreenRecordingPermission();
    m_hasAccessibilityPermission = PlatformFeatures::hasAccessibilityPermission();
#endif

    // Advanced
    m_shortcutHintsEnabled = RegionCaptureSettingsManager::instance().isShortcutHintsEnabled();
#ifdef SNAPTRAY_ENABLE_MCP
    m_mcpEnabled = MCPSettingsManager::instance().isEnabled();
#endif

    auto blurOpts = AutoBlurSettingsManager::instance().load();
    m_blurIntensity = blurOpts.blurIntensity;
    m_blurType = static_cast<int>(blurOpts.blurType);

    auto& pinMgr = PinWindowSettingsManager::instance();
    m_pinDefaultOpacity = qRound(pinMgr.loadDefaultOpacity() * 100.0);
    m_pinOpacityStep = qRound(pinMgr.loadOpacityStep() * 100.0);
    m_pinZoomStep = qRound(pinMgr.loadZoomStep() * 100.0);
    m_pinMaxCacheFiles = pinMgr.loadMaxCacheFiles();

    // Watermark
    auto wmSettings = WatermarkSettingsManager::instance().load();
    m_watermarkEnabled = wmSettings.enabled;
    m_watermarkApplyToRecording = wmSettings.applyToRecording;
    m_watermarkImagePath = wmSettings.imagePath;
    m_watermarkScale = wmSettings.imageScale;
    m_watermarkOpacity = qRound(wmSettings.opacity * 100.0);
    m_watermarkMargin = wmSettings.margin;
    m_watermarkPosition = static_cast<int>(wmSettings.position);

    // Recording
    auto& recMgr = RecordingSettingsManager::instance();
    m_recordingFrameRate = recMgr.frameRate();
    m_recordingOutputFormat = recMgr.outputFormat();
    m_recordingQuality = recMgr.quality();
    m_recordingShowPreview = recMgr.showPreview();
    m_recordingAudioEnabled = recMgr.audioEnabled();
    m_recordingAudioSource = recMgr.audioSource();
    m_countdownEnabled = recMgr.countdownEnabled();
    m_countdownSeconds = recMgr.countdownSeconds();

    // Files
    auto& fileMgr = FileSettingsManager::instance();
    m_screenshotPath = fileMgr.loadScreenshotPath();
    m_recordingPath = fileMgr.loadRecordingPath();
    m_filenameTemplate = fileMgr.loadFilenameTemplate();
    m_autoSaveScreenshots = fileMgr.loadAutoSaveScreenshots();
    m_autoSaveRecordings = fileMgr.loadAutoSaveRecordings();

    // Updates
    auto& updateMgr = UpdateSettingsManager::instance();
    m_autoCheckUpdates = updateMgr.isAutoCheckEnabled();
    m_checkFrequencyHours = updateMgr.checkIntervalHours();
}

// ─────────────────────────────────────────────────────────────────────────────
// General property accessors
// ─────────────────────────────────────────────────────────────────────────────

bool SettingsBackend::startOnLogin() const { return m_startOnLogin; }
void SettingsBackend::setStartOnLogin(bool v) {
    if (m_startOnLogin != v) { m_startOnLogin = v; emit startOnLoginChanged(); }
}

QString SettingsBackend::language() const { return m_language; }
void SettingsBackend::setLanguage(const QString& v) {
    if (m_language != v) { m_language = v; emit languageChanged(); }
}

QVariantList SettingsBackend::availableLanguages() const
{
    QVariantList result;
    const auto langs = LanguageManager::instance().availableLanguages();
    for (const auto& lang : langs) {
        QVariantMap entry;
        entry[QStringLiteral("code")] = lang.code;
        entry[QStringLiteral("text")] = lang.nativeName;
        result.append(entry);
    }
    return result;
}

int SettingsBackend::appTheme() const { return m_appTheme; }
void SettingsBackend::setAppTheme(int v) {
    if (m_appTheme != v) {
        m_appTheme = v;
        // Apply preview immediately but keep Save/Cancel semantics by restoring
        // the original persisted style in cancel() when needed.
        ToolbarStyleConfig::saveStyle(static_cast<ToolbarStyleType>(v));
        ThemeManager::instance().refreshTheme();
        emit appThemeChanged();
    }
}

bool SettingsBackend::cliInstalled() const { return m_cliInstalled; }

bool SettingsBackend::isMacOS() const
{
#ifdef Q_OS_MAC
    return true;
#else
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// macOS Permissions
// ─────────────────────────────────────────────────────────────────────────────

#ifdef Q_OS_MAC
bool SettingsBackend::hasScreenRecordingPermission() const { return m_hasScreenRecordingPermission; }
bool SettingsBackend::hasAccessibilityPermission() const { return m_hasAccessibilityPermission; }
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Advanced property accessors
// ─────────────────────────────────────────────────────────────────────────────

bool SettingsBackend::shortcutHintsEnabled() const { return m_shortcutHintsEnabled; }

bool SettingsBackend::isMcpBuild() const
{
#ifdef SNAPTRAY_ENABLE_MCP
    return true;
#else
    return false;
#endif
}

void SettingsBackend::setShortcutHintsEnabled(bool v) {
    if (m_shortcutHintsEnabled != v) { m_shortcutHintsEnabled = v; emit shortcutHintsEnabledChanged(); }
}

#ifdef SNAPTRAY_ENABLE_MCP
bool SettingsBackend::mcpEnabled() const { return m_mcpEnabled; }
void SettingsBackend::setMcpEnabled(bool v) {
    if (m_mcpEnabled != v) { m_mcpEnabled = v; emit mcpEnabledChanged(v); }
}
#endif

int SettingsBackend::blurIntensity() const { return m_blurIntensity; }
void SettingsBackend::setBlurIntensity(int v) {
    if (m_blurIntensity != v) { m_blurIntensity = v; emit blurIntensityChanged(); }
}

int SettingsBackend::blurType() const { return m_blurType; }
void SettingsBackend::setBlurType(int v) {
    if (m_blurType != v) { m_blurType = v; emit blurTypeChanged(); }
}

int SettingsBackend::pinDefaultOpacity() const { return m_pinDefaultOpacity; }
void SettingsBackend::setPinDefaultOpacity(int v) {
    if (m_pinDefaultOpacity != v) { m_pinDefaultOpacity = v; emit pinDefaultOpacityChanged(); }
}

int SettingsBackend::pinOpacityStep() const { return m_pinOpacityStep; }
void SettingsBackend::setPinOpacityStep(int v) {
    if (m_pinOpacityStep != v) { m_pinOpacityStep = v; emit pinOpacityStepChanged(); }
}

int SettingsBackend::pinZoomStep() const { return m_pinZoomStep; }
void SettingsBackend::setPinZoomStep(int v) {
    if (m_pinZoomStep != v) { m_pinZoomStep = v; emit pinZoomStepChanged(); }
}

int SettingsBackend::pinMaxCacheFiles() const { return m_pinMaxCacheFiles; }
void SettingsBackend::setPinMaxCacheFiles(int v) {
    if (m_pinMaxCacheFiles != v) { m_pinMaxCacheFiles = v; emit pinMaxCacheFilesChanged(); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Watermark property accessors
// ─────────────────────────────────────────────────────────────────────────────

bool SettingsBackend::watermarkEnabled() const { return m_watermarkEnabled; }
void SettingsBackend::setWatermarkEnabled(bool v) {
    if (m_watermarkEnabled != v) { m_watermarkEnabled = v; emit watermarkEnabledChanged(); }
}

bool SettingsBackend::watermarkApplyToRecording() const { return m_watermarkApplyToRecording; }
void SettingsBackend::setWatermarkApplyToRecording(bool v) {
    if (m_watermarkApplyToRecording != v) { m_watermarkApplyToRecording = v; emit watermarkApplyToRecordingChanged(); }
}

QString SettingsBackend::watermarkImagePath() const { return m_watermarkImagePath; }
void SettingsBackend::setWatermarkImagePath(const QString& v) {
    if (m_watermarkImagePath != v) { m_watermarkImagePath = v; emit watermarkImagePathChanged(); }
}

int SettingsBackend::watermarkScale() const { return m_watermarkScale; }
void SettingsBackend::setWatermarkScale(int v) {
    if (m_watermarkScale != v) { m_watermarkScale = v; emit watermarkScaleChanged(); }
}

int SettingsBackend::watermarkOpacity() const { return m_watermarkOpacity; }
void SettingsBackend::setWatermarkOpacity(int v) {
    if (m_watermarkOpacity != v) { m_watermarkOpacity = v; emit watermarkOpacityChanged(); }
}

int SettingsBackend::watermarkMargin() const { return m_watermarkMargin; }
void SettingsBackend::setWatermarkMargin(int v) {
    if (m_watermarkMargin != v) { m_watermarkMargin = v; emit watermarkMarginChanged(); }
}

int SettingsBackend::watermarkPosition() const { return m_watermarkPosition; }
void SettingsBackend::setWatermarkPosition(int v) {
    if (m_watermarkPosition != v) { m_watermarkPosition = v; emit watermarkPositionChanged(); }
}

QString SettingsBackend::watermarkPreviewUrl() const
{
    if (m_watermarkImagePath.isEmpty())
        return {};
    return QUrl::fromLocalFile(m_watermarkImagePath).toString();
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording property accessors
// ─────────────────────────────────────────────────────────────────────────────

int SettingsBackend::recordingFrameRate() const { return m_recordingFrameRate; }
void SettingsBackend::setRecordingFrameRate(int v) {
    if (m_recordingFrameRate != v) { m_recordingFrameRate = v; emit recordingFrameRateChanged(); }
}

int SettingsBackend::recordingOutputFormat() const { return m_recordingOutputFormat; }
void SettingsBackend::setRecordingOutputFormat(int v) {
    if (m_recordingOutputFormat != v) { m_recordingOutputFormat = v; emit recordingOutputFormatChanged(); }
}

int SettingsBackend::recordingQuality() const { return m_recordingQuality; }
void SettingsBackend::setRecordingQuality(int v) {
    if (m_recordingQuality != v) { m_recordingQuality = v; emit recordingQualityChanged(); }
}

bool SettingsBackend::recordingShowPreview() const { return m_recordingShowPreview; }
void SettingsBackend::setRecordingShowPreview(bool v) {
    if (m_recordingShowPreview != v) { m_recordingShowPreview = v; emit recordingShowPreviewChanged(); }
}

bool SettingsBackend::recordingAudioEnabled() const { return m_recordingAudioEnabled; }
void SettingsBackend::setRecordingAudioEnabled(bool v) {
    if (m_recordingAudioEnabled != v) { m_recordingAudioEnabled = v; emit recordingAudioEnabledChanged(); }
}

int SettingsBackend::recordingAudioSource() const { return m_recordingAudioSource; }
void SettingsBackend::setRecordingAudioSource(int v) {
    if (m_recordingAudioSource != v) { m_recordingAudioSource = v; emit recordingAudioSourceChanged(); }
}

bool SettingsBackend::countdownEnabled() const { return m_countdownEnabled; }
void SettingsBackend::setCountdownEnabled(bool v) {
    if (m_countdownEnabled != v) { m_countdownEnabled = v; emit countdownEnabledChanged(); }
}

int SettingsBackend::countdownSeconds() const { return m_countdownSeconds; }
void SettingsBackend::setCountdownSeconds(int v) {
    if (m_countdownSeconds != v) { m_countdownSeconds = v; emit countdownSecondsChanged(); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Files property accessors
// ─────────────────────────────────────────────────────────────────────────────

QString SettingsBackend::screenshotPath() const { return m_screenshotPath; }
void SettingsBackend::setScreenshotPath(const QString& v) {
    if (m_screenshotPath != v) { m_screenshotPath = v; emit screenshotPathChanged(); }
}

QString SettingsBackend::recordingPath() const { return m_recordingPath; }
void SettingsBackend::setRecordingPath(const QString& v) {
    if (m_recordingPath != v) { m_recordingPath = v; emit recordingPathChanged(); }
}

QString SettingsBackend::filenameTemplate() const { return m_filenameTemplate; }
void SettingsBackend::setFilenameTemplate(const QString& v) {
    if (m_filenameTemplate != v) { m_filenameTemplate = v; emit filenameTemplateChanged(); }
}

QString SettingsBackend::filenamePreview() const
{
    return computeFilenamePreview();
}

bool SettingsBackend::autoSaveScreenshots() const { return m_autoSaveScreenshots; }
void SettingsBackend::setAutoSaveScreenshots(bool v) {
    if (m_autoSaveScreenshots != v) { m_autoSaveScreenshots = v; emit autoSaveScreenshotsChanged(); }
}

bool SettingsBackend::autoSaveRecordings() const { return m_autoSaveRecordings; }
void SettingsBackend::setAutoSaveRecordings(bool v) {
    if (m_autoSaveRecordings != v) { m_autoSaveRecordings = v; emit autoSaveRecordingsChanged(); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Updates property accessors
// ─────────────────────────────────────────────────────────────────────────────

QString SettingsBackend::currentVersion() const
{
    return UpdateChecker::currentVersion();
}

bool SettingsBackend::autoCheckUpdates() const { return m_autoCheckUpdates; }
void SettingsBackend::setAutoCheckUpdates(bool v) {
    if (m_autoCheckUpdates != v) { m_autoCheckUpdates = v; emit autoCheckUpdatesChanged(); }
}

int SettingsBackend::checkFrequencyHours() const { return m_checkFrequencyHours; }
void SettingsBackend::setCheckFrequencyHours(int v) {
    if (m_checkFrequencyHours != v) { m_checkFrequencyHours = v; emit checkFrequencyHoursChanged(); }
}

QString SettingsBackend::lastCheckedText() const
{
    const QDateTime lastCheck = UpdateSettingsManager::instance().lastCheckTime();
    if (!lastCheck.isValid())
        return tr("Never");
    return QLocale().toString(lastCheck, QLocale::ShortFormat);
}

bool SettingsBackend::isCheckingForUpdates() const { return m_isCheckingForUpdates; }

// ─────────────────────────────────────────────────────────────────────────────
// About property accessors
// ─────────────────────────────────────────────────────────────────────────────

QString SettingsBackend::appName() const
{
    return QString::fromLatin1(SnapTray::kApplicationName);
}

QString SettingsBackend::appVersion() const
{
    return UpdateChecker::currentVersion();
}

// ─────────────────────────────────────────────────────────────────────────────
// save() — flush all buffered values to settings managers
// ─────────────────────────────────────────────────────────────────────────────

void SettingsBackend::save()
{
    const bool languageChanged = (m_language != m_originalLanguage);

    // General
    AutoLaunchManager::setEnabled(m_startOnLogin);
    LanguageManager::instance().saveLanguage(m_language);
    m_originalLanguage = m_language;
    ToolbarStyleConfig::saveStyle(static_cast<ToolbarStyleType>(m_appTheme));
    m_originalAppTheme = m_appTheme;

    // Advanced
    RegionCaptureSettingsManager::instance().setShortcutHintsEnabled(m_shortcutHintsEnabled);
#ifdef SNAPTRAY_ENABLE_MCP
    MCPSettingsManager::instance().setEnabled(m_mcpEnabled);
#endif

    AutoBlurSettingsManager::Options blurOpts;
    blurOpts.blurIntensity = m_blurIntensity;
    blurOpts.blurType = static_cast<AutoBlurSettingsManager::BlurType>(m_blurType);
    blurOpts.enabled = AutoBlurSettingsManager::instance().loadEnabled();
    blurOpts.detectFaces = AutoBlurSettingsManager::instance().loadDetectFaces();
    AutoBlurSettingsManager::instance().save(blurOpts);

    auto& pinMgr = PinWindowSettingsManager::instance();
    pinMgr.saveDefaultOpacity(m_pinDefaultOpacity / 100.0);
    pinMgr.saveOpacityStep(m_pinOpacityStep / 100.0);
    pinMgr.saveZoomStep(m_pinZoomStep / 100.0);
    pinMgr.saveMaxCacheFiles(m_pinMaxCacheFiles);

    // Watermark
    WatermarkSettingsManager::Settings wmSettings;
    wmSettings.enabled = m_watermarkEnabled;
    wmSettings.applyToRecording = m_watermarkApplyToRecording;
    wmSettings.imagePath = m_watermarkImagePath;
    wmSettings.imageScale = m_watermarkScale;
    wmSettings.opacity = m_watermarkOpacity / 100.0;
    wmSettings.margin = m_watermarkMargin;
    wmSettings.position = static_cast<WatermarkSettingsManager::Position>(m_watermarkPosition);
    WatermarkSettingsManager::instance().save(wmSettings);

    // Recording
    auto& recMgr = RecordingSettingsManager::instance();
    recMgr.setFrameRate(m_recordingFrameRate);
    recMgr.setOutputFormat(m_recordingOutputFormat);
    recMgr.setQuality(m_recordingQuality);
    recMgr.setShowPreview(m_recordingShowPreview);
    recMgr.setAudioEnabled(m_recordingAudioEnabled);
    recMgr.setAudioSource(m_recordingAudioSource);
    recMgr.setCountdownEnabled(m_countdownEnabled);
    recMgr.setCountdownSeconds(m_countdownSeconds);

    // Files
    auto& fileMgr = FileSettingsManager::instance();
    fileMgr.saveScreenshotPath(m_screenshotPath);
    fileMgr.saveRecordingPath(m_recordingPath);
    fileMgr.saveFilenameTemplate(m_filenameTemplate);
    fileMgr.saveAutoSaveScreenshots(m_autoSaveScreenshots);
    fileMgr.saveAutoSaveRecordings(m_autoSaveRecordings);

    // Updates
    auto& updateMgr = UpdateSettingsManager::instance();
    updateMgr.setAutoCheckEnabled(m_autoCheckUpdates);
    updateMgr.setCheckIntervalHours(m_checkFrequencyHours);

    // OCR (if loaded)
    if (m_ocrLoaded) {
        auto& ocrMgr = OCRSettingsManager::instance();
        ocrMgr.setLanguages(m_ocrSelectedLanguages);
        ocrMgr.setBehavior(static_cast<OCRBehavior>(m_ocrBehavior));
        ocrMgr.save();
        emit ocrLanguagesChanged(m_ocrSelectedLanguages);
    }

    emit settingsSaved(languageChanged);
}

void SettingsBackend::cancel()
{
    if (m_appTheme != m_originalAppTheme) {
        ToolbarStyleConfig::saveStyle(static_cast<ToolbarStyleType>(m_originalAppTheme));
        ThemeManager::instance().refreshTheme();
        m_appTheme = m_originalAppTheme;
        emit appThemeChanged();
    }
    emit settingsCancelled();
}

// ─────────────────────────────────────────────────────────────────────────────
// CLI
// ─────────────────────────────────────────────────────────────────────────────

void SettingsBackend::installCLI()
{
    if (PlatformFeatures::instance().installCLI()) {
        m_cliInstalled = true;
        emit cliInstalledChanged();
    }
}

void SettingsBackend::uninstallCLI()
{
    if (PlatformFeatures::instance().uninstallCLI()) {
        m_cliInstalled = false;
        emit cliInstalledChanged();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// macOS Permissions
// ─────────────────────────────────────────────────────────────────────────────

#ifdef Q_OS_MAC
void SettingsBackend::openScreenRecordingSettings()
{
    PlatformFeatures::openScreenRecordingSettings();
}

void SettingsBackend::openAccessibilitySettings()
{
    PlatformFeatures::openAccessibilitySettings();
}

void SettingsBackend::refreshPermissions()
{
    m_hasScreenRecordingPermission = PlatformFeatures::hasScreenRecordingPermission();
    m_hasAccessibilityPermission = PlatformFeatures::hasAccessibilityPermission();
    emit permissionsChanged();
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Browse dialogs
// ─────────────────────────────────────────────────────────────────────────────

void SettingsBackend::browseScreenshotPath()
{
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr, tr("Select Screenshot Folder"), m_screenshotPath);
    if (!dir.isEmpty())
        setScreenshotPath(dir);
}

void SettingsBackend::browseRecordingPath()
{
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr, tr("Select Recording Folder"), m_recordingPath);
    if (!dir.isEmpty())
        setRecordingPath(dir);
}

void SettingsBackend::browseWatermarkImage()
{
    const QString file = QFileDialog::getOpenFileName(
        nullptr, tr("Select Watermark Image"), m_watermarkImagePath,
        tr("Images (*.png *.jpg *.jpeg *.bmp *.svg)"));
    if (!file.isEmpty())
        setWatermarkImagePath(file);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update checking
// ─────────────────────────────────────────────────────────────────────────────

void SettingsBackend::checkForUpdates()
{
    if (m_isCheckingForUpdates)
        return;

    if (!m_updateChecker) {
        m_updateChecker = new UpdateChecker(this);

        connect(m_updateChecker, &UpdateChecker::checkStarted, this, [this]() {
            m_isCheckingForUpdates = true;
            emit isCheckingForUpdatesChanged();
        });

        connect(m_updateChecker, &UpdateChecker::updateAvailable, this,
                [this](const ReleaseInfo& release) {
                    m_isCheckingForUpdates = false;
                    emit isCheckingForUpdatesChanged();
                    emit lastCheckedTextChanged();
                    emit updateAvailable(release.version, release.releaseNotes);
                });

        connect(m_updateChecker, &UpdateChecker::noUpdateAvailable, this, [this]() {
            m_isCheckingForUpdates = false;
            emit isCheckingForUpdatesChanged();
            emit lastCheckedTextChanged();
            emit noUpdateAvailable();
        });

        connect(m_updateChecker, &UpdateChecker::checkFailed, this,
                [this](const QString& error) {
                    m_isCheckingForUpdates = false;
                    emit isCheckingForUpdatesChanged();
                    emit updateCheckFailed(error);
                });
    }

    m_updateChecker->checkForUpdates(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// OCR
// ─────────────────────────────────────────────────────────────────────────────

void SettingsBackend::loadOcrLanguages()
{
    if (m_ocrLoaded)
        return;

    auto& ocrMgr = OCRSettingsManager::instance();
    ocrMgr.load();
    m_ocrSelectedLanguages = ocrMgr.languages();
    m_ocrBehavior = static_cast<int>(ocrMgr.behavior());
    m_ocrLoaded = true;
    emit ocrLanguagesChanged(m_ocrSelectedLanguages);
}

QVariantList SettingsBackend::ocrAvailableLanguages() const
{
    QVariantList result;
    if (!PlatformFeatures::instance().isOCRAvailable())
        return result;

    const auto langs = OCRManager::availableLanguages();
    for (const auto& lang : langs) {
        QVariantMap entry;
        entry[QStringLiteral("code")] = lang.code;
        entry[QStringLiteral("displayName")] = lang.nativeName;
        result.append(entry);
    }
    return result;
}

QVariantList SettingsBackend::ocrSelectedLanguages() const
{
    QVariantList result;
    if (!PlatformFeatures::instance().isOCRAvailable())
        return result;

    const auto allLangs = OCRManager::availableLanguages();
    for (const auto& code : m_ocrSelectedLanguages) {
        QVariantMap entry;
        entry[QStringLiteral("code")] = code;
        // Find display name
        QString displayName = code;
        for (const auto& lang : allLangs) {
            if (lang.code == code) {
                displayName = lang.nativeName;
                break;
            }
        }
        entry[QStringLiteral("displayName")] = displayName;
        result.append(entry);
    }
    return result;
}

void SettingsBackend::addOcrLanguage(const QString& code)
{
    if (!m_ocrSelectedLanguages.contains(code)) {
        m_ocrSelectedLanguages.append(code);
        emit ocrLanguagesChanged(m_ocrSelectedLanguages);
    }
}

void SettingsBackend::removeOcrLanguage(const QString& code)
{
    if (m_ocrSelectedLanguages.removeAll(code) > 0) {
        emit ocrLanguagesChanged(m_ocrSelectedLanguages);
    }
}

void SettingsBackend::moveOcrLanguage(int from, int to)
{
    if (from < 0 || from >= m_ocrSelectedLanguages.size()
        || to < 0 || to >= m_ocrSelectedLanguages.size()
        || from == to)
        return;
    m_ocrSelectedLanguages.move(from, to);
    emit ocrLanguagesChanged(m_ocrSelectedLanguages);
}

int SettingsBackend::ocrBehavior() const { return m_ocrBehavior; }

void SettingsBackend::setOcrBehavior(int behavior)
{
    m_ocrBehavior = behavior;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hotkeys
// ─────────────────────────────────────────────────────────────────────────────

QVariantList SettingsBackend::hotkeyCategories() const
{
    QVariantList result;
    static const HotkeyCategory categories[] = {
        HotkeyCategory::Capture,
        HotkeyCategory::Canvas,
        HotkeyCategory::Clipboard,
        HotkeyCategory::Pin,
        HotkeyCategory::Recording,
    };
    for (auto cat : categories) {
        QVariantMap entry;
        entry[QStringLiteral("category")] = static_cast<int>(cat);
        entry[QStringLiteral("name")] = getCategoryDisplayName(cat);
        result.append(entry);
    }
    return result;
}

QVariantList SettingsBackend::hotkeysForCategory(int category) const
{
    QVariantList result;
    auto& mgr = HotkeyManager::instance();
    const auto configs = mgr.getConfigsByCategory(static_cast<HotkeyCategory>(category));
    for (const auto& config : configs) {
        QVariantMap entry;
        entry[QStringLiteral("action")] = static_cast<int>(config.action);
        entry[QStringLiteral("name")] = config.displayName;
        entry[QStringLiteral("keySequence")] = HotkeyManager::formatKeySequence(config.keySequence);
        entry[QStringLiteral("status")] = static_cast<int>(config.status);
        result.append(entry);
    }
    return result;
}

void SettingsBackend::editHotkey(int action)
{
    auto& mgr = HotkeyManager::instance();
    auto config = mgr.getConfig(static_cast<HotkeyAction>(action));

    TypeHotkeyDialog dialog;
    dialog.setActionName(config.displayName);
    dialog.setInitialKeySequence(config.keySequence);

    mgr.suspendRegistration();
    const int dialogResult = dialog.exec();
    mgr.resumeRegistration();

    if (dialogResult == QDialog::Accepted) {
        mgr.updateHotkey(static_cast<HotkeyAction>(action), dialog.keySequence());
        emit hotkeysChanged();
    }
}

void SettingsBackend::clearHotkey(int action)
{
    auto& mgr = HotkeyManager::instance();
    mgr.updateHotkey(static_cast<HotkeyAction>(action), QString());
    emit hotkeysChanged();
}

void SettingsBackend::resetHotkey(int action)
{
    auto& mgr = HotkeyManager::instance();
    mgr.resetToDefault(static_cast<HotkeyAction>(action));
    emit hotkeysChanged();
}

void SettingsBackend::restoreAllHotkeys()
{
    HotkeyManager::instance().resetAllToDefaults();
    emit hotkeysChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio devices
// ─────────────────────────────────────────────────────────────────────────────

QVariantList SettingsBackend::audioDevices() const
{
    QVariantList result;
    auto engine = std::unique_ptr<IAudioCaptureEngine>(
        IAudioCaptureEngine::createBestEngine());
    if (!engine)
        return result;

    const auto devices = engine->availableInputDevices();
    for (const auto& device : devices) {
        QVariantMap entry;
        entry[QStringLiteral("id")] = device.id;
        entry[QStringLiteral("name")] = device.name;
        result.append(entry);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

QString SettingsBackend::computeFilenamePreview() const
{
    FilenameTemplateEngine::Context ctx;
    ctx.type = QStringLiteral("Screenshot");
    ctx.prefix = FileSettingsManager::instance().loadFilenamePrefix();
    ctx.dateFormat = FileSettingsManager::instance().loadDateFormat();
    ctx.ext = QStringLiteral("png");
    ctx.width = 1920;
    ctx.height = 1080;

    auto result = FilenameTemplateEngine::renderFilename(m_filenameTemplate, ctx);
    return result.filename;
}

} // namespace SnapTray
