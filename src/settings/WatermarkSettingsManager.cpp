#include "settings/WatermarkSettingsManager.h"
#include "settings/Settings.h"

#include <QSettings>

WatermarkSettingsManager& WatermarkSettingsManager::instance()
{
    static WatermarkSettingsManager instance;
    return instance;
}

WatermarkSettingsManager::Settings WatermarkSettingsManager::load() const
{
    Settings settings;
    settings.enabled = loadEnabled();
    settings.imagePath = loadImagePath();
    settings.opacity = loadOpacity();
    settings.position = loadPosition();
    settings.imageScale = loadImageScale();
    settings.margin = loadMargin();
    settings.applyToRecording = loadApplyToRecording();
    return settings;
}

void WatermarkSettingsManager::save(const Settings& settings)
{
    saveEnabled(settings.enabled);
    saveImagePath(settings.imagePath);
    saveOpacity(settings.opacity);
    savePosition(settings.position);
    saveImageScale(settings.imageScale);
    saveMargin(settings.margin);
    saveApplyToRecording(settings.applyToRecording);
}

bool WatermarkSettingsManager::loadEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyEnabled, kDefaultEnabled).toBool();
}

void WatermarkSettingsManager::saveEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyEnabled, enabled);
}

QString WatermarkSettingsManager::loadImagePath() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyImagePath, QString()).toString();
}

void WatermarkSettingsManager::saveImagePath(const QString& path)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyImagePath, path);
}

qreal WatermarkSettingsManager::loadOpacity() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyOpacity, kDefaultOpacity).toDouble();
}

void WatermarkSettingsManager::saveOpacity(qreal opacity)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyOpacity, opacity);
}

WatermarkSettingsManager::Position WatermarkSettingsManager::loadPosition() const
{
    auto settings = SnapTray::getSettings();
    return static_cast<Position>(settings.value(kKeyPosition, static_cast<int>(kDefaultPosition)).toInt());
}

void WatermarkSettingsManager::savePosition(Position position)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyPosition, static_cast<int>(position));
}

int WatermarkSettingsManager::loadImageScale() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyImageScale, kDefaultImageScale).toInt();
}

void WatermarkSettingsManager::saveImageScale(int scale)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyImageScale, scale);
}

int WatermarkSettingsManager::loadMargin() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyMargin, kDefaultMargin).toInt();
}

void WatermarkSettingsManager::saveMargin(int margin)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyMargin, margin);
}

bool WatermarkSettingsManager::loadApplyToRecording() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyApplyToRecording, kDefaultApplyToRecording).toBool();
}

void WatermarkSettingsManager::saveApplyToRecording(bool apply)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyApplyToRecording, apply);
}
