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

int AnnotationSettingsManager::loadCornerRadius() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyCornerRadius, kDefaultCornerRadius).toInt();
}

void AnnotationSettingsManager::saveCornerRadius(int radius)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyCornerRadius, radius);
}

bool AnnotationSettingsManager::loadCornerRadiusEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyCornerRadiusEnabled, kDefaultCornerRadiusEnabled).toBool();
}

void AnnotationSettingsManager::saveCornerRadiusEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyCornerRadiusEnabled, enabled);
}

bool AnnotationSettingsManager::loadAspectRatioLocked() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyAspectRatioLocked, kDefaultAspectRatioLocked).toBool();
}

void AnnotationSettingsManager::saveAspectRatioLocked(bool locked)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyAspectRatioLocked, locked);
}

int AnnotationSettingsManager::loadAspectRatioWidth() const
{
    auto settings = SnapTray::getSettings();
    int width = settings.value(kSettingsKeyAspectRatioWidth, kDefaultAspectRatioWidth).toInt();
    return width > 0 ? width : kDefaultAspectRatioWidth;
}

int AnnotationSettingsManager::loadAspectRatioHeight() const
{
    auto settings = SnapTray::getSettings();
    int height = settings.value(kSettingsKeyAspectRatioHeight, kDefaultAspectRatioHeight).toInt();
    return height > 0 ? height : kDefaultAspectRatioHeight;
}

void AnnotationSettingsManager::saveAspectRatio(int width, int height)
{
    auto settings = SnapTray::getSettings();
    if (width > 0 && height > 0) {
        settings.setValue(kSettingsKeyAspectRatioWidth, width);
        settings.setValue(kSettingsKeyAspectRatioHeight, height);
    }
}

MosaicBlurTypeSection::BlurType AnnotationSettingsManager::loadMosaicBlurType() const
{
    auto settings = SnapTray::getSettings();
    int value = settings.value(kSettingsKeyMosaicBlurType,
                               static_cast<int>(kDefaultMosaicBlurType)).toInt();
    if (value >= 0 && value <= 1) {
        return static_cast<MosaicBlurTypeSection::BlurType>(value);
    }
    return kDefaultMosaicBlurType;
}

void AnnotationSettingsManager::saveMosaicBlurType(MosaicBlurTypeSection::BlurType type)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyMosaicBlurType, static_cast<int>(type));
}
