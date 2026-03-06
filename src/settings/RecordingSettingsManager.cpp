#include "settings/RecordingSettingsManager.h"
#include "settings/Settings.h"

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
