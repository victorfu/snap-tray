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
#include <QFutureWatcher>
#include <QLocale>
#include <QUrl>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

namespace {
QString normalizeRecordingAudioInputDeviceId(const QString& deviceId)
{
    if (deviceId.isEmpty()) {
        return QString();
    }

    std::unique_ptr<IAudioCaptureEngine> engine(IAudioCaptureEngine::createBestEngine(nullptr));
    if (!engine) {
        qWarning() << "SettingsBackend: Cannot validate audio device, audio engine unavailable";
        return deviceId;
    }

    const auto devices = engine->availableInputDevices();
    for (const auto& device : devices) {
        if (device.id == deviceId) {
            return deviceId;
        }
    }

    return QString();
}

QVariantList queryRecordingAudioDevices()
{
    QVariantList result;
    auto engine = std::unique_ptr<IAudioCaptureEngine>(
        IAudioCaptureEngine::createBestEngine());
    if (!engine) {
        return result;
    }

    const auto devices = engine->availableInputDevices();
    for (const auto& device : devices) {
        QVariantMap entry;
        entry[QStringLiteral("text")] = device.name;
        entry[QStringLiteral("value")] = device.id;
        entry[QStringLiteral("isDefault")] = device.isDefault;
        result.append(entry);
    }
    return result;
}

int blurTypeToUiIndex(AutoBlurSettingsManager::BlurType type)
{
    return (type == AutoBlurSettingsManager::BlurType::Gaussian) ? 1 : 0;
}

AutoBlurSettingsManager::BlurType blurTypeFromUiIndex(int index)
{
    return (index == 1)
        ? AutoBlurSettingsManager::BlurType::Gaussian
        : AutoBlurSettingsManager::BlurType::Pixelate;
}
}

namespace SnapTray {

SettingsBackend::SettingsBackend(QObject* parent)
    : QObject(parent)
{
    loadAllSettings();
}

SettingsBackend::~SettingsBackend() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Load all settings from managers into the current view model state
// ─────────────────────────────────────────────────────────────────────────────

void SettingsBackend::loadAllSettings()
{
    // General
    m_startOnLogin = AutoLaunchManager::isEnabled();
    m_language = LanguageManager::instance().loadLanguage();
    m_appTheme = static_cast<int>(ToolbarStyleConfig::loadStyle());
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
    m_blurType = blurTypeToUiIndex(blurOpts.blurType);

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
    m_recordingAudioDevice = recMgr.audioDevice();
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
    if (m_startOnLogin != v) {
        m_startOnLogin = v;
        AutoLaunchManager::setEnabled(v);
        emit startOnLoginChanged();
    }
}

QString SettingsBackend::language() const { return m_language; }
void SettingsBackend::setLanguage(const QString& v) {
    if (m_language != v) {
        m_language = v;
        LanguageManager::instance().saveLanguage(v);
        emit languageChanged();
    }
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
    if (m_shortcutHintsEnabled != v) {
        m_shortcutHintsEnabled = v;
        RegionCaptureSettingsManager::instance().setShortcutHintsEnabled(v);
        emit shortcutHintsEnabledChanged();
    }
}

#ifdef SNAPTRAY_ENABLE_MCP
bool SettingsBackend::mcpEnabled() const { return m_mcpEnabled; }
void SettingsBackend::setMcpEnabled(bool v) {
    if (m_mcpEnabled != v) {
        m_mcpEnabled = v;
        MCPSettingsManager::instance().setEnabled(v);
        emit mcpEnabledChanged(v);
    }
}
#endif

int SettingsBackend::blurIntensity() const { return m_blurIntensity; }
void SettingsBackend::setBlurIntensity(int v) {
    if (m_blurIntensity != v) {
        m_blurIntensity = v;
        AutoBlurSettingsManager::instance().saveBlurIntensity(v);
        emit blurIntensityChanged();
    }
}

int SettingsBackend::blurType() const { return m_blurType; }
void SettingsBackend::setBlurType(int v) {
    v = qBound(0, v, 1);
    if (m_blurType != v) {
        m_blurType = v;
        AutoBlurSettingsManager::instance().saveBlurType(blurTypeFromUiIndex(v));
        emit blurTypeChanged();
    }
}

int SettingsBackend::pinDefaultOpacity() const { return m_pinDefaultOpacity; }
void SettingsBackend::setPinDefaultOpacity(int v) {
    if (m_pinDefaultOpacity != v) {
        m_pinDefaultOpacity = v;
        PinWindowSettingsManager::instance().saveDefaultOpacity(v / 100.0);
        emit pinDefaultOpacityChanged();
    }
}

int SettingsBackend::pinOpacityStep() const { return m_pinOpacityStep; }
void SettingsBackend::setPinOpacityStep(int v) {
    if (m_pinOpacityStep != v) {
        m_pinOpacityStep = v;
        PinWindowSettingsManager::instance().saveOpacityStep(v / 100.0);
        emit pinOpacityStepChanged();
    }
}

int SettingsBackend::pinZoomStep() const { return m_pinZoomStep; }
void SettingsBackend::setPinZoomStep(int v) {
    if (m_pinZoomStep != v) {
        m_pinZoomStep = v;
        PinWindowSettingsManager::instance().saveZoomStep(v / 100.0);
        emit pinZoomStepChanged();
    }
}

int SettingsBackend::pinMaxCacheFiles() const { return m_pinMaxCacheFiles; }
void SettingsBackend::setPinMaxCacheFiles(int v) {
    if (m_pinMaxCacheFiles != v) {
        m_pinMaxCacheFiles = v;
        PinWindowSettingsManager::instance().saveMaxCacheFiles(v);
        emit pinMaxCacheFilesChanged();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Watermark property accessors
// ─────────────────────────────────────────────────────────────────────────────

bool SettingsBackend::watermarkEnabled() const { return m_watermarkEnabled; }
void SettingsBackend::setWatermarkEnabled(bool v) {
    if (m_watermarkEnabled != v) {
        m_watermarkEnabled = v;
        WatermarkSettingsManager::instance().saveEnabled(v);
        emit watermarkEnabledChanged();
    }
}

bool SettingsBackend::watermarkApplyToRecording() const { return m_watermarkApplyToRecording; }
void SettingsBackend::setWatermarkApplyToRecording(bool v) {
    if (m_watermarkApplyToRecording != v) {
        m_watermarkApplyToRecording = v;
        WatermarkSettingsManager::instance().saveApplyToRecording(v);
        emit watermarkApplyToRecordingChanged();
    }
}

QString SettingsBackend::watermarkImagePath() const { return m_watermarkImagePath; }
void SettingsBackend::setWatermarkImagePath(const QString& v) {
    if (m_watermarkImagePath != v) {
        m_watermarkImagePath = v;
        WatermarkSettingsManager::instance().saveImagePath(v);
        emit watermarkImagePathChanged();
    }
}

int SettingsBackend::watermarkScale() const { return m_watermarkScale; }
void SettingsBackend::setWatermarkScale(int v) {
    if (m_watermarkScale != v) {
        m_watermarkScale = v;
        WatermarkSettingsManager::instance().saveImageScale(v);
        emit watermarkScaleChanged();
    }
}

int SettingsBackend::watermarkOpacity() const { return m_watermarkOpacity; }
void SettingsBackend::setWatermarkOpacity(int v) {
    if (m_watermarkOpacity != v) {
        m_watermarkOpacity = v;
        WatermarkSettingsManager::instance().saveOpacity(v / 100.0);
        emit watermarkOpacityChanged();
    }
}

int SettingsBackend::watermarkMargin() const { return m_watermarkMargin; }
void SettingsBackend::setWatermarkMargin(int v) {
    if (m_watermarkMargin != v) {
        m_watermarkMargin = v;
        WatermarkSettingsManager::instance().saveMargin(v);
        emit watermarkMarginChanged();
    }
}

int SettingsBackend::watermarkPosition() const { return m_watermarkPosition; }
void SettingsBackend::setWatermarkPosition(int v) {
    if (m_watermarkPosition != v) {
        m_watermarkPosition = v;
        WatermarkSettingsManager::instance().savePosition(
            static_cast<WatermarkSettingsManager::Position>(v));
        emit watermarkPositionChanged();
    }
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
    if (m_recordingFrameRate != v) {
        m_recordingFrameRate = v;
        RecordingSettingsManager::instance().setFrameRate(v);
        emit recordingFrameRateChanged();
    }
}

int SettingsBackend::recordingOutputFormat() const { return m_recordingOutputFormat; }
void SettingsBackend::setRecordingOutputFormat(int v) {
    v = qBound(0, v, 2);
    if (m_recordingOutputFormat == v)
        return;

    m_recordingOutputFormat = v;
    RecordingSettingsManager::instance().setOutputFormat(v);
    emit recordingOutputFormatChanged();
}

int SettingsBackend::recordingQuality() const { return m_recordingQuality; }
void SettingsBackend::setRecordingQuality(int v) {
    if (m_recordingQuality != v) {
        m_recordingQuality = v;
        RecordingSettingsManager::instance().setQuality(v);
        emit recordingQualityChanged();
    }
}

bool SettingsBackend::recordingShowPreview() const { return m_recordingShowPreview; }
void SettingsBackend::setRecordingShowPreview(bool v) {
    if (m_recordingShowPreview == v)
        return;

    m_recordingShowPreview = v;
    RecordingSettingsManager::instance().setShowPreview(v);
    emit recordingShowPreviewChanged();
}

bool SettingsBackend::recordingAudioEnabled() const { return m_recordingAudioEnabled; }
void SettingsBackend::setRecordingAudioEnabled(bool v) {
    if (m_recordingAudioEnabled != v) {
        m_recordingAudioEnabled = v;
        RecordingSettingsManager::instance().setAudioEnabled(v);
        emit recordingAudioEnabledChanged();
    }
}

int SettingsBackend::recordingAudioSource() const { return m_recordingAudioSource; }
void SettingsBackend::setRecordingAudioSource(int v) {
    v = qBound(0, v, 2);
    if (m_recordingAudioSource != v) {
        m_recordingAudioSource = v;
        RecordingSettingsManager::instance().setAudioSource(v);
        emit recordingAudioSourceChanged();
    }
}

QString SettingsBackend::recordingAudioDevice() const { return m_recordingAudioDevice; }
void SettingsBackend::setRecordingAudioDevice(const QString& v) {
    if (m_recordingAudioDevice != v) {
        m_recordingAudioDevice = v;
        RecordingSettingsManager::instance().setAudioDevice(m_recordingAudioDevice);
        emit recordingAudioDeviceChanged();
    }
}

bool SettingsBackend::recordingAudioDevicesLoading() const
{
    return m_recordingAudioDevicesLoading;
}

bool SettingsBackend::recordingAudioDevicesLoaded() const
{
    return m_recordingAudioDevicesLoaded;
}

QVariantList SettingsBackend::recordingAudioDevices() const
{
    return m_recordingAudioDeviceItems;
}

bool SettingsBackend::countdownEnabled() const { return m_countdownEnabled; }
void SettingsBackend::setCountdownEnabled(bool v) {
    if (m_countdownEnabled != v) {
        m_countdownEnabled = v;
        RecordingSettingsManager::instance().setCountdownEnabled(v);
        emit countdownEnabledChanged();
    }
}

int SettingsBackend::countdownSeconds() const { return m_countdownSeconds; }
void SettingsBackend::setCountdownSeconds(int v) {
    if (m_countdownSeconds != v) {
        m_countdownSeconds = v;
        RecordingSettingsManager::instance().setCountdownSeconds(v);
        emit countdownSecondsChanged();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Files property accessors
// ─────────────────────────────────────────────────────────────────────────────

QString SettingsBackend::screenshotPath() const { return m_screenshotPath; }
void SettingsBackend::setScreenshotPath(const QString& v) {
    if (m_screenshotPath != v) {
        m_screenshotPath = v;
        FileSettingsManager::instance().saveScreenshotPath(v);
        emit screenshotPathChanged();
    }
}

QString SettingsBackend::recordingPath() const { return m_recordingPath; }
void SettingsBackend::setRecordingPath(const QString& v) {
    if (m_recordingPath != v) {
        m_recordingPath = v;
        FileSettingsManager::instance().saveRecordingPath(v);
        emit recordingPathChanged();
    }
}

QString SettingsBackend::filenameTemplate() const { return m_filenameTemplate; }
void SettingsBackend::setFilenameTemplate(const QString& v) {
    if (m_filenameTemplate != v) {
        m_filenameTemplate = v;
        FileSettingsManager::instance().saveFilenameTemplate(v);
        emit filenameTemplateChanged();
    }
}

QString SettingsBackend::filenamePreview() const
{
    return computeFilenamePreview();
}

bool SettingsBackend::autoSaveScreenshots() const { return m_autoSaveScreenshots; }
void SettingsBackend::setAutoSaveScreenshots(bool v) {
    if (m_autoSaveScreenshots != v) {
        m_autoSaveScreenshots = v;
        FileSettingsManager::instance().saveAutoSaveScreenshots(v);
        emit autoSaveScreenshotsChanged();
    }
}

bool SettingsBackend::autoSaveRecordings() const { return m_autoSaveRecordings; }
void SettingsBackend::setAutoSaveRecordings(bool v) {
    if (m_autoSaveRecordings != v) {
        m_autoSaveRecordings = v;
        FileSettingsManager::instance().saveAutoSaveRecordings(v);
        emit autoSaveRecordingsChanged();
    }
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
    if (m_autoCheckUpdates != v) {
        m_autoCheckUpdates = v;
        UpdateSettingsManager::instance().setAutoCheckEnabled(v);
        emit autoCheckUpdatesChanged();
    }
}

int SettingsBackend::checkFrequencyHours() const { return m_checkFrequencyHours; }
void SettingsBackend::setCheckFrequencyHours(int v) {
    if (m_checkFrequencyHours != v) {
        m_checkFrequencyHours = v;
        UpdateSettingsManager::instance().setCheckIntervalHours(v);
        emit checkFrequencyHoursChanged();
    }
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

bool SettingsBackend::ocrSupported() const
{
    return PlatformFeatures::instance().isOCRAvailable();
}

bool SettingsBackend::ocrLoading() const
{
    return m_ocrLoading;
}

bool SettingsBackend::ocrAvailableLanguagesLoaded() const
{
    return !ocrSupported() || m_ocrAvailableLanguagesLoaded;
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
    ensureOcrSettingsLoaded();

    if (!ocrSupported() || m_ocrAvailableLanguagesLoaded || m_ocrLoading)
        return;

    if (!m_ocrLanguageWatcher) {
        m_ocrLanguageWatcher = new QFutureWatcher<OCRLanguageQueryResult>(this);
        connect(m_ocrLanguageWatcher, &QFutureWatcher<OCRLanguageQueryResult>::finished,
                this, [this]() {
            const OCRLanguageQueryResult queryResult = m_ocrLanguageWatcher->result();
            const auto& languages = queryResult.languages;
            m_ocrAvailableLanguageItems.clear();
            m_ocrDisplayNamesByCode.clear();

            for (const auto& lang : languages) {
                QVariantMap entry;
                entry[QStringLiteral("code")] = lang.code;
                entry[QStringLiteral("displayName")] = lang.nativeName;
                m_ocrAvailableLanguageItems.append(entry);
                m_ocrDisplayNamesByCode.insert(lang.code, lang.nativeName);
            }

            const bool loadedChanged = !m_ocrAvailableLanguagesLoaded;
            // A completed query, even an unsuccessful one, should unblock the
            // page so the user sees the empty/fallback state instead of an
            // infinite loading spinner.
            m_ocrAvailableLanguagesLoaded = true;
            m_ocrLoading = false;
            emit ocrLoadingChanged();
            if (loadedChanged)
                emit ocrAvailableLanguagesLoadedChanged();
            emit ocrAvailableLanguagesChanged();
            emit ocrSelectedLanguagesChanged();
        });
    }

    m_ocrLoading = true;
    emit ocrLoadingChanged();
    m_ocrLanguageWatcher->setFuture(QtConcurrent::run([]() {
        return OCRManager::queryAvailableLanguages();
    }));
}

void SettingsBackend::loadRecordingAudioDevices()
{
    if (m_recordingAudioDevicesLoaded || m_recordingAudioDevicesLoading)
        return;

    if (!m_recordingAudioDevicesWatcher) {
        m_recordingAudioDevicesWatcher = new QFutureWatcher<QVariantList>(this);
        connect(m_recordingAudioDevicesWatcher, &QFutureWatcher<QVariantList>::finished,
                this, [this]() {
            m_recordingAudioDeviceItems = m_recordingAudioDevicesWatcher->result();

            const bool loadedChanged = !m_recordingAudioDevicesLoaded;
            m_recordingAudioDevicesLoaded = true;
            m_recordingAudioDevicesLoading = false;
            const bool deviceChanged = normalizeRecordingAudioSettings();
            if (deviceChanged) {
                RecordingSettingsManager::instance().setAudioDevice(m_recordingAudioDevice);
            }
            emit recordingAudioDevicesLoadingChanged();
            if (loadedChanged)
                emit recordingAudioDevicesLoadedChanged();
            emit recordingAudioDevicesChanged();
        });
    }

    m_recordingAudioDevicesLoading = true;
    emit recordingAudioDevicesLoadingChanged();
    m_recordingAudioDevicesWatcher->setFuture(QtConcurrent::run([]() {
        return queryRecordingAudioDevices();
    }));
}

QVariantList SettingsBackend::ocrAvailableLanguages() const
{
    return m_ocrAvailableLanguageItems;
}

QVariantList SettingsBackend::ocrSelectedLanguages() const
{
    QVariantList result;
    if (!ocrSupported())
        return result;

    for (const auto& code : m_ocrSelectedLanguages) {
        QVariantMap entry;
        entry[QStringLiteral("code")] = code;
        entry[QStringLiteral("displayName")] = ocrDisplayNameForCode(code);
        result.append(entry);
    }
    return result;
}

void SettingsBackend::addOcrLanguage(const QString& code)
{
    ensureOcrSettingsLoaded();
    if (!m_ocrSelectedLanguages.contains(code)) {
        m_ocrSelectedLanguages.append(code);
        persistOcrSettings();
        emit ocrSelectedLanguagesChanged();
        emit ocrLanguagesChanged(m_ocrSelectedLanguages);
    }
}

void SettingsBackend::removeOcrLanguage(const QString& code)
{
    ensureOcrSettingsLoaded();
    if (m_ocrSelectedLanguages.removeAll(code) > 0) {
        persistOcrSettings();
        emit ocrSelectedLanguagesChanged();
        emit ocrLanguagesChanged(m_ocrSelectedLanguages);
    }
}

void SettingsBackend::moveOcrLanguage(int from, int to)
{
    ensureOcrSettingsLoaded();
    if (from < 0 || from >= m_ocrSelectedLanguages.size()
        || to < 0 || to >= m_ocrSelectedLanguages.size()
        || from == to)
        return;
    m_ocrSelectedLanguages.move(from, to);
    persistOcrSettings();
    emit ocrSelectedLanguagesChanged();
    emit ocrLanguagesChanged(m_ocrSelectedLanguages);
}

int SettingsBackend::ocrBehavior() const { return m_ocrBehavior; }

void SettingsBackend::setOcrBehavior(int behavior)
{
    ensureOcrSettingsLoaded();
    if (m_ocrBehavior == behavior)
        return;

    m_ocrBehavior = behavior;
    persistOcrSettings();
    emit ocrBehaviorChanged();
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
    return m_recordingAudioDeviceItems;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

bool SettingsBackend::normalizeRecordingAudioSettings()
{
    QString normalized = m_recordingAudioDevice;
    if (!normalized.isEmpty()) {
        const QString canonical = normalizeRecordingAudioInputDeviceId(normalized);
        if (!canonical.isEmpty()) {
            normalized = canonical;
        }
    }

    if (m_recordingAudioDevice == normalized) {
        return false;
    }

    m_recordingAudioDevice = normalized;
    emit recordingAudioDeviceChanged();
    return true;
}

bool SettingsBackend::hasRecordingAudioDevice(const QString& deviceId) const
{
    for (const auto& deviceValue : m_recordingAudioDeviceItems) {
        const QVariantMap device = deviceValue.toMap();
        if (device.value(QStringLiteral("value")).toString() == deviceId) {
            return true;
        }
    }

    return false;
}

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

void SettingsBackend::ensureOcrSettingsLoaded()
{
    if (m_ocrLoaded)
        return;

    auto& ocrMgr = OCRSettingsManager::instance();
    ocrMgr.load();
    m_ocrSelectedLanguages = ocrMgr.languages();
    m_ocrBehavior = static_cast<int>(ocrMgr.behavior());
    m_ocrLoaded = true;
    emit ocrSelectedLanguagesChanged();
    emit ocrBehaviorChanged();
}

void SettingsBackend::persistOcrSettings()
{
    auto& ocrMgr = OCRSettingsManager::instance();
    ocrMgr.setLanguages(m_ocrSelectedLanguages);
    ocrMgr.setBehavior(static_cast<OCRBehavior>(m_ocrBehavior));
    ocrMgr.save();
    m_ocrSelectedLanguages = ocrMgr.languages();
    m_ocrBehavior = static_cast<int>(ocrMgr.behavior());
}

QString SettingsBackend::ocrDisplayNameForCode(const QString& code) const
{
    const auto it = m_ocrDisplayNamesByCode.constFind(code);
    if (it != m_ocrDisplayNamesByCode.cend())
        return it.value();

    return code;
}

} // namespace SnapTray
