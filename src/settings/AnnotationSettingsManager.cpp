#include "settings/AnnotationSettingsManager.h"
#include <QSettings>

AnnotationSettingsManager& AnnotationSettingsManager::instance()
{
    static AnnotationSettingsManager instance;
    return instance;
}

QColor AnnotationSettingsManager::loadColor() const
{
    QSettings settings("Victor Fu", "SnapTray");
    return settings.value(kSettingsKeyColor, defaultColor()).value<QColor>();
}

void AnnotationSettingsManager::saveColor(const QColor& color)
{
    QSettings settings("Victor Fu", "SnapTray");
    settings.setValue(kSettingsKeyColor, color);
}

int AnnotationSettingsManager::loadWidth() const
{
    QSettings settings("Victor Fu", "SnapTray");
    return settings.value(kSettingsKeyWidth, kDefaultWidth).toInt();
}

void AnnotationSettingsManager::saveWidth(int width)
{
    QSettings settings("Victor Fu", "SnapTray");
    settings.setValue(kSettingsKeyWidth, width);
}
