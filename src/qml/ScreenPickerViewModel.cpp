#include "qml/ScreenPickerViewModel.h"
#include "recording/KeyProbe.h"

#include "qml/DialogImageProvider.h"
#include "recording/RecordingAudioStatus.h"
#include "recording/ScreenSourceService.h"
#include "settings/RecordingSettingsManager.h"
#include "PlatformFeatures.h"

#include <memory>

#include <QScreen>
#include <QVariantMap>
#include <utility>

namespace {

QString tooltipTextForSource(const SnapTray::ScreenSource& source)
{
    if (source.name.isEmpty()) {
        return source.resolutionText;
    }
    if (source.resolutionText.isEmpty()) {
        return source.name;
    }

    return QStringLiteral("%1 (%2)").arg(source.name, source.resolutionText);
}

} // namespace

namespace SnapTray {

namespace {

constexpr int kOutputFormatGif = 1;
constexpr int kOutputFormatWebP = 2;

const char* outputFormatName(int format)
{
    switch (format) {
    case kOutputFormatGif: return "GIF";
    case kOutputFormatWebP: return "WebP";
    default: return "MP4";
    }
}

struct MonitorStatusText {
    SnapTray::AudioLevelMonitor::State state;
    const char* text;
};

constexpr MonitorStatusText kMonitorStatusTexts[] = {
    {SnapTray::AudioLevelMonitor::State::Idle, ""},
    {SnapTray::AudioLevelMonitor::State::WaitingForPermission,
     QT_TRANSLATE_NOOP("SnapTray::ScreenPickerViewModel", "Waiting for microphone permission...")},
    {SnapTray::AudioLevelMonitor::State::Listening,
     QT_TRANSLATE_NOOP("SnapTray::ScreenPickerViewModel", "Speak to test the microphone")},
    {SnapTray::AudioLevelMonitor::State::PermissionDenied,
     QT_TRANSLATE_NOOP("SnapTray::ScreenPickerViewModel", "Microphone permission denied")},
    {SnapTray::AudioLevelMonitor::State::DeviceMissing,
     QT_TRANSLATE_NOOP("SnapTray::ScreenPickerViewModel", "Selected microphone not found")},
    {SnapTray::AudioLevelMonitor::State::Unavailable,
     QT_TRANSLATE_NOOP("SnapTray::ScreenPickerViewModel", "Microphone unavailable")},
};

} // namespace

ScreenPickerViewModel::ScreenPickerViewModel(QObject* parent)
    : QObject(parent)
{
    loadAudioSettings();
    connect(&m_levelMonitor, &AudioLevelMonitor::stateChanged,
            this, &ScreenPickerViewModel::microphoneTestChanged);
    connect(&m_levelMonitor, &AudioLevelMonitor::levelChanged,
            this, &ScreenPickerViewModel::microphoneLevelChanged);
}

ScreenPickerViewModel::ScreenPickerViewModel(const QVector<ScreenSourceInfo>& sources,
                                             QObject* parent)
    : ScreenPickerViewModel(parent)
{
    setSources(sources);
}

ScreenPickerViewModel::~ScreenPickerViewModel()
{
    m_levelMonitor.stop();
    clearThumbnails();
}

void ScreenPickerViewModel::loadAudioSettings()
{
    const auto& settings = RecordingSettingsManager::instance();
    const bool enabled = settings.audioEnabled();
    const int source = settings.audioSource();
    m_microphoneEnabled = enabled && (source == kAudioSourceMicrophone || source == kAudioSourceBoth);
    m_systemAudioEnabled = enabled && (source == kAudioSourceSystem || source == kAudioSourceBoth);
    m_selectedDevice = settings.audioDevice();
}

void ScreenPickerViewModel::saveAudioSettings()
{
    auto& settings = RecordingSettingsManager::instance();
    settings.setAudioEnabled(m_microphoneEnabled || m_systemAudioEnabled);
    if (m_microphoneEnabled && m_systemAudioEnabled) {
        settings.setAudioSource(kAudioSourceBoth);
    } else if (m_microphoneEnabled) {
        settings.setAudioSource(kAudioSourceMicrophone);
    } else if (m_systemAudioEnabled) {
        settings.setAudioSource(kAudioSourceSystem);
    }
    settings.setAudioDevice(m_selectedDevice);
    emit audioSettingsChanged();
}

void ScreenPickerViewModel::setMicrophoneEnabled(bool enabled)
{
    if (m_microphoneEnabled == enabled) return;
    m_microphoneEnabled = enabled;
    if (!enabled) stopMicrophoneTest();
    saveAudioSettings();
}

void ScreenPickerViewModel::setSystemAudioEnabled(bool enabled)
{
    if (m_systemAudioEnabled == enabled) return;
    m_systemAudioEnabled = enabled;
    saveAudioSettings();
}

void ScreenPickerViewModel::setSelectedDevice(const QString& deviceId)
{
    if (m_selectedDevice == deviceId) return;
    m_selectedDevice = deviceId;
    saveAudioSettings();
    if (m_levelMonitor.state() != AudioLevelMonitor::State::Idle) {
        m_levelMonitor.start(m_selectedDevice);
    }
}

bool ScreenPickerViewModel::outputHasAudio() const
{
    const auto& settings = RecordingSettingsManager::instance();
    return recordingCapturesAudio(settings.outputFormat(), editMode());
}

bool ScreenPickerViewModel::recordingModeSupported() const
{
    return !PlatformFeatures::instance().capabilities().recordingDirectMp4Only;
}

bool ScreenPickerViewModel::editMode() const
{
    return recordingModeSupported() && RecordingSettingsManager::instance().showPreview();
}

void ScreenPickerViewModel::setEditMode(bool edit)
{
    if (!recordingModeSupported() || editMode() == edit) return;
    RecordingSettingsManager::instance().setShowPreview(edit);
    emit recordingModeChanged();
}

int ScreenPickerViewModel::quickSaveAction() const
{
    return RecordingSettingsManager::instance().quickSaveAction();
}

void ScreenPickerViewModel::setQuickSaveAction(int action)
{
    if (quickSaveAction() == action) return;
    RecordingSettingsManager::instance().setQuickSaveAction(action);
    emit recordingModeChanged();
}

QString ScreenPickerViewModel::outputSummary() const
{
    const auto& settings = RecordingSettingsManager::instance();
    const QString format = QString::fromLatin1(outputFormatName(settings.outputFormat()));
    const QString preview = editMode()
        ? tr("Preview after recording")
        : tr("Saved without preview");
    const QString countdown = settings.countdownEnabled() && settings.countdownSeconds() > 0
        ? tr("Countdown: %1 s").arg(settings.countdownSeconds())
        : tr("No countdown");
    return QStringLiteral("%1 \u00B7 %2 \u00B7 %3").arg(format, preview, countdown);
}

QString ScreenPickerViewModel::audioNote() const
{
    const auto& settings = RecordingSettingsManager::instance();
    if (!outputHasAudio()) {
        return tr("GIF and WebP recordings have no sound. Turn on preview or choose MP4 to record audio.");
    }
    if (editMode() && settings.outputFormat() != 0) {
        return tr("Audio is recorded for the preview; GIF and WebP exports are silent.");
    }
    return {};
}

bool ScreenPickerViewModel::showSetupBeforeRecording() const
{
    return RecordingSettingsManager::instance().showSetupBeforeRecording();
}

void ScreenPickerViewModel::setShowSetupBeforeRecording(bool show)
{
    if (showSetupBeforeRecording() == show) return;
    RecordingSettingsManager::instance().setShowSetupBeforeRecording(show);
    emit showSetupBeforeRecordingChanged();
}

bool ScreenPickerViewModel::keyRecordingSupported() const
{
    return SnapTray::keyRecordingSupported();
}

bool ScreenPickerViewModel::recordShortcutKeys() const
{
    return keyRecordingSupported() && RecordingSettingsManager::instance().keyRecordingMode() != 0;
}

void ScreenPickerViewModel::setRecordShortcutKeys(bool value)
{
    if (!keyRecordingSupported() || recordShortcutKeys() == value) return;
    RecordingSettingsManager::instance().setKeyRecordingMode(value ? 1 : 0);
    emit keyRecordingChanged();
}

bool ScreenPickerViewModel::recordTypedText() const
{
    return keyRecordingSupported() && RecordingSettingsManager::instance().keyRecordingMode() == 2;
}

void ScreenPickerViewModel::setRecordTypedText(bool value)
{
    if (!recordShortcutKeys() || recordTypedText() == value) return;
    RecordingSettingsManager::instance().setKeyRecordingMode(value ? 2 : 1);
    emit keyRecordingChanged();
}

void ScreenPickerViewModel::setAudioEngineFactory(AudioLevelMonitor::EngineFactory factory)
{
    m_engineFactory = factory;
    m_levelMonitor.setEngineFactory(std::move(factory));
}

void ScreenPickerViewModel::loadDevices()
{
    std::unique_ptr<IAudioCaptureEngine> engine(
        m_engineFactory ? m_engineFactory() : IAudioCaptureEngine::createBestEngine(nullptr));
    m_inputDevices.clear();
    bool selectedFound = m_selectedDevice.isEmpty();
    if (engine) {
        m_systemAudioSupported = engine->isSystemAudioSupported();
        for (const auto& device : engine->availableInputDevices()) {
            QVariantMap entry;
            entry[QStringLiteral("text")] = device.name;
            entry[QStringLiteral("value")] = device.id;
            entry[QStringLiteral("isDefault")] = device.isDefault;
            m_inputDevices.append(entry);
            selectedFound = selectedFound || device.id == m_selectedDevice;
        }
    } else {
        m_systemAudioSupported = false;
    }
    if (!selectedFound) {
        // Keep the saved choice visible as disconnected instead of silently
        // switching; recording falls back to the default input and says so.
        QVariantMap entry;
        entry[QStringLiteral("text")] = tr("Not connected: %1").arg(m_selectedDevice);
        entry[QStringLiteral("value")] = m_selectedDevice;
        entry[QStringLiteral("isDefault")] = false;
        entry[QStringLiteral("missing")] = true;
        m_inputDevices.append(entry);
    }
    m_devicesLoaded = true;
    emit devicesChanged();
}

void ScreenPickerViewModel::toggleMicrophoneTest()
{
    if (microphoneTestActive()) {
        stopMicrophoneTest();
        return;
    }
    if (!m_microphoneEnabled) return;
    m_levelMonitor.start(m_selectedDevice);
}

void ScreenPickerViewModel::stopMicrophoneTest()
{
    m_levelMonitor.stop();
}

bool ScreenPickerViewModel::microphoneTestActive() const
{
    return m_levelMonitor.state() != AudioLevelMonitor::State::Idle;
}

QString ScreenPickerViewModel::microphoneTestStatus() const
{
    for (const auto& entry : kMonitorStatusTexts) {
        if (entry.state == m_levelMonitor.state()) {
            return *entry.text ? tr(entry.text) : QString();
        }
    }
    return {};
}

void ScreenPickerViewModel::setSources(const QVector<ScreenSourceInfo>& sources)
{
    clearThumbnails();
    m_screens.clear();
    m_screenRefs.clear();
    m_screenIds.clear();
    m_thumbnailIds.clear();

    for (int index = 0; index < sources.size(); ++index) {
        const auto& source = sources.at(index);
        if (!source.screen) {
            continue;
        }

        const QString thumbnailId = QStringLiteral("screen_picker_%1").arg(index);
        if (!source.thumbnail.isNull()) {
            DialogImageProvider::setImage(thumbnailId, source.thumbnail);
            m_thumbnailIds.append(thumbnailId);
        }

        QVariantMap map;
        map[QStringLiteral("name")] = source.name;
        map[QStringLiteral("resolution")] = source.resolutionText;
        map[QStringLiteral("tooltip")] = tooltipTextForSource(source);
        map[QStringLiteral("thumbnailId")] = thumbnailId;
        map[QStringLiteral("hasThumbnail")] = !source.thumbnail.isNull();
        map[QStringLiteral("isPrimary")] = source.isPrimary;
        m_screens.append(map);
        m_screenRefs.append(source.screen);
        m_screenIds.append(source.id);
    }

    ++m_thumbnailCacheBuster;
    emit thumbnailCacheBusterChanged();
    emit screensChanged();
}

void ScreenPickerViewModel::chooseScreen(int index)
{
    if (index < 0 || index >= m_screenRefs.size()) {
        return;
    }

    QScreen* screen = m_screenRefs.at(index).data();
    if (!screen && index < m_screenIds.size()) {
        screen = ScreenSourceService::screenForId(m_screenIds.at(index));
        if (screen) {
            m_screenRefs[index] = screen;
        }
    }

    // Release the test microphone before recording opens its own capture.
    m_levelMonitor.stop();
    emit screenChosen(screen);
}

void ScreenPickerViewModel::cancel()
{
    m_levelMonitor.stop();
    emit cancelled();
}

void ScreenPickerViewModel::clearThumbnails()
{
    for (const QString& thumbnailId : std::as_const(m_thumbnailIds)) {
        DialogImageProvider::removeImage(thumbnailId);
    }
    m_thumbnailIds.clear();
}

} // namespace SnapTray
