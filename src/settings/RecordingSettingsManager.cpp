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
