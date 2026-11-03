#include "settings/RecordingSettingsManager.h"
#include "settings/Settings.h"

#include <QJsonDocument>

RecordingSettingsManager& RecordingSettingsManager::instance()
{
    static RecordingSettingsManager instance;
    return instance;
}

int RecordingSettingsManager::frameRate() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyFrameRate, kDefaultFrameRate).toInt();
}

void RecordingSettingsManager::setFrameRate(int value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyFrameRate, value);
}

int RecordingSettingsManager::outputFormat() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyOutputFormat, kDefaultOutputFormat).toInt();
}

void RecordingSettingsManager::setOutputFormat(int value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyOutputFormat, value);
}

int RecordingSettingsManager::quality() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyQuality, kDefaultQuality).toInt();
}

void RecordingSettingsManager::setQuality(int value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyQuality, value);
}

bool RecordingSettingsManager::showPreview() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyShowPreview, kDefaultShowPreview).toBool();
}

void RecordingSettingsManager::setShowPreview(bool value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyShowPreview, value);
}

bool RecordingSettingsManager::audioEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyAudioEnabled, kDefaultAudioEnabled).toBool();
}

void RecordingSettingsManager::setAudioEnabled(bool value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyAudioEnabled, value);
}

int RecordingSettingsManager::audioSource() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyAudioSource, kDefaultAudioSource).toInt();
}

void RecordingSettingsManager::setAudioSource(int value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyAudioSource, value);
}

QString RecordingSettingsManager::audioDevice() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyAudioDevice, kDefaultAudioDevice).toString();
}

void RecordingSettingsManager::setAudioDevice(const QString& value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyAudioDevice, value);
}

bool RecordingSettingsManager::countdownEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyCountdownEnabled, kDefaultCountdownEnabled).toBool();
}

void RecordingSettingsManager::setCountdownEnabled(bool value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyCountdownEnabled, value);
}

int RecordingSettingsManager::countdownSeconds() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyCountdownSeconds, kDefaultCountdownSeconds).toInt();
}

void RecordingSettingsManager::setCountdownSeconds(int value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyCountdownSeconds, value);
}

bool RecordingSettingsManager::showSetupBeforeRecording() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyShowSetupBeforeRecording, kDefaultShowSetupBeforeRecording).toBool();
}

void RecordingSettingsManager::setShowSetupBeforeRecording(bool value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyShowSetupBeforeRecording, value);
}

bool RecordingSettingsManager::nativeFrameEncoding() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyNativeFrameEncoding, kDefaultNativeFrameEncoding).toBool();
}

void RecordingSettingsManager::setNativeFrameEncoding(bool value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyNativeFrameEncoding, value);
}

int RecordingSettingsManager::keyRecordingMode() const
{
    auto settings = SnapTray::getSettings();
    const int mode = settings.value(kKeyKeyRecordingMode, 0).toInt();
    return mode >= 0 && mode <= 2 ? mode : 0;
}

void RecordingSettingsManager::setKeyRecordingMode(int value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyKeyRecordingMode, qBound(0, value, 2));
}

bool RecordingSettingsManager::preselectArea() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyPreselectArea, false).toBool();
}

void RecordingSettingsManager::setPreselectArea(bool value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyPreselectArea, value);
}

int RecordingSettingsManager::quickSaveAction() const
{
    auto settings = SnapTray::getSettings();
    const int action = settings.value(kKeyQuickSaveAction, int(QuickSaveNothing)).toInt();
    return action >= QuickSaveNothing && action <= QuickSaveShowInFolder ? action : int(QuickSaveNothing);
}

void RecordingSettingsManager::setQuickSaveAction(int action)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyQuickSaveAction,
                      action >= QuickSaveNothing && action <= QuickSaveShowInFolder ? action : int(QuickSaveNothing));
}

bool RecordingSettingsManager::keepProjects() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyKeepProjects, false).toBool();
}

void RecordingSettingsManager::setKeepProjects(bool value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyKeepProjects, value);
}

int RecordingSettingsManager::projectCleanupDays() const
{
    auto settings = SnapTray::getSettings();
    return qMax(0, settings.value(kKeyProjectCleanupDays, 0).toInt());
}

void RecordingSettingsManager::setProjectCleanupDays(int days)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyProjectCleanupDays, qMax(0, days));
}

QJsonObject RecordingSettingsManager::keyOverlay() const
{
    auto settings = SnapTray::getSettings();
    return QJsonDocument::fromJson(settings.value(kKeyKeyOverlay).toByteArray()).object();
}

void RecordingSettingsManager::setKeyOverlay(const QJsonObject& value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyKeyOverlay, QJsonDocument(value).toJson(QJsonDocument::Compact));
}

QJsonObject RecordingSettingsManager::pointerEffects() const
{
    auto settings = SnapTray::getSettings();
    return QJsonDocument::fromJson(settings.value(kKeyPointerEffects).toByteArray()).object();
}

void RecordingSettingsManager::setPointerEffects(const QJsonObject& value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyPointerEffects, QJsonDocument(value).toJson(QJsonDocument::Compact));
}

QJsonObject RecordingSettingsManager::mp4ExportOptions() const
{
    auto settings = SnapTray::getSettings();
    return QJsonDocument::fromJson(settings.value(kKeyMp4ExportOptions).toByteArray()).object();
}

void RecordingSettingsManager::setMp4ExportOptions(const QJsonObject& value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyMp4ExportOptions, QJsonDocument(value).toJson(QJsonDocument::Compact));
}

QJsonObject RecordingSettingsManager::appearance() const
{
    auto settings = SnapTray::getSettings();
    return QJsonDocument::fromJson(settings.value(kKeyAppearance).toByteArray()).object();
}

void RecordingSettingsManager::setAppearance(const QJsonObject& value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyAppearance, QJsonDocument(value).toJson(QJsonDocument::Compact));
}

bool RecordingSettingsManager::windowsGraphicsCapture() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyWindowsGraphicsCapture, kDefaultWindowsGraphicsCapture).toBool();
}

void RecordingSettingsManager::setWindowsGraphicsCapture(bool value)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyWindowsGraphicsCapture, value);
}
