#include "settings/PinWindowSettingsManager.h"
#include "settings/Settings.h"
#include <QSettings>

PinWindowSettingsManager& PinWindowSettingsManager::instance()
{
    static PinWindowSettingsManager instance;
    return instance;
}

qreal PinWindowSettingsManager::loadDefaultOpacity() const
{
    auto settings = SnapTray::getSettings();
    qreal opacity = settings.value(kSettingsKeyDefaultOpacity, kDefaultOpacity).toDouble();
    // Clamp to valid range
    if (opacity < 0.1) opacity = 0.1;
    if (opacity > 1.0) opacity = 1.0;
    return opacity;
}

void PinWindowSettingsManager::saveDefaultOpacity(qreal opacity)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyDefaultOpacity, opacity);
}

qreal PinWindowSettingsManager::loadOpacityStep() const
{
    auto settings = SnapTray::getSettings();
    qreal step = settings.value(kSettingsKeyOpacityStep, kDefaultOpacityStep).toDouble();
    // Clamp to valid range
    if (step < 0.01) step = 0.01;
    if (step > 0.20) step = 0.20;
    return step;
}

void PinWindowSettingsManager::saveOpacityStep(qreal step)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyOpacityStep, step);
}

qreal PinWindowSettingsManager::loadZoomStep() const
{
    auto settings = SnapTray::getSettings();
    qreal step = settings.value(kSettingsKeyZoomStep, kDefaultZoomStep).toDouble();
    // Clamp to valid range
    if (step < 0.01) step = 0.01;
    if (step > 0.20) step = 0.20;
    return step;
}

void PinWindowSettingsManager::saveZoomStep(qreal step)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyZoomStep, step);
}

bool PinWindowSettingsManager::loadShadowEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyShadowEnabled, kDefaultShadowEnabled).toBool();
}

void PinWindowSettingsManager::saveShadowEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyShadowEnabled, enabled);
}

int PinWindowSettingsManager::loadMaxCacheFiles() const
{
    auto settings = SnapTray::getSettings();
    int maxFiles = settings.value(kSettingsKeyMaxCacheFiles, kDefaultMaxCacheFiles).toInt();
    if (maxFiles < 5) maxFiles = 5;
    if (maxFiles > 200) maxFiles = 200;
    return maxFiles;
}

void PinWindowSettingsManager::saveMaxCacheFiles(int maxFiles)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyMaxCacheFiles, maxFiles);
}
