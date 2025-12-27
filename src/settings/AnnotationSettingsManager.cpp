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
