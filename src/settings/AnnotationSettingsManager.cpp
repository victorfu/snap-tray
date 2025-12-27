#include "settings/AnnotationSettingsManager.h"
#include "settings/Settings.h"
#include <QSettings>

AnnotationSettingsManager& AnnotationSettingsManager::instance()
{
    static AnnotationSettingsManager instance;
    return instance;
}

QColor AnnotationSettingsManager::loadColor() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyColor, defaultColor()).value<QColor>();
}

void AnnotationSettingsManager::saveColor(const QColor& color)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyColor, color);
}

int AnnotationSettingsManager::loadWidth() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyWidth, kDefaultWidth).toInt();
}

void AnnotationSettingsManager::saveWidth(int width)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyWidth, width);
}

StepBadgeSize AnnotationSettingsManager::loadStepBadgeSize() const
{
    auto settings = SnapTray::getSettings();
    int value = settings.value(kSettingsKeyStepBadgeSize,
                               static_cast<int>(kDefaultStepBadgeSize)).toInt();
    if (value >= 0 && value <= 2) {
        return static_cast<StepBadgeSize>(value);
    }
    return kDefaultStepBadgeSize;
}

void AnnotationSettingsManager::saveStepBadgeSize(StepBadgeSize size)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyStepBadgeSize, static_cast<int>(size));
}

bool AnnotationSettingsManager::loadPolylineMode() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyPolylineMode, kDefaultPolylineMode).toBool();
}

void AnnotationSettingsManager::savePolylineMode(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyPolylineMode, enabled);
}
