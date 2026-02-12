#include "settings/BeautifySettingsManager.h"
#include "settings/Settings.h"
#include <QSettings>
#include <algorithm>

BeautifySettingsManager& BeautifySettingsManager::instance()
{
    static BeautifySettingsManager instance;
    return instance;
}

BeautifySettings BeautifySettingsManager::loadSettings() const
{
    BeautifySettings s;
    s.backgroundType = loadBackgroundType();
    s.backgroundColor = loadBackgroundColor();
    s.gradientStartColor = loadGradientStartColor();
    s.gradientEndColor = loadGradientEndColor();
    s.gradientAngle = loadGradientAngle();
    s.padding = loadPadding();
    s.cornerRadius = loadCornerRadius();
    s.shadowEnabled = loadShadowEnabled();
    s.shadowBlur = loadShadowBlur();
    s.shadowOffsetX = loadShadowOffsetX();
    s.shadowOffsetY = loadShadowOffsetY();
    s.shadowColor = loadShadowColor();
    s.aspectRatio = loadAspectRatio();
    return s;
}

void BeautifySettingsManager::saveSettings(const BeautifySettings& s)
{
    saveBackgroundType(s.backgroundType);
    saveBackgroundColor(s.backgroundColor);
    saveGradientStartColor(s.gradientStartColor);
    saveGradientEndColor(s.gradientEndColor);
    saveGradientAngle(s.gradientAngle);
    savePadding(s.padding);
    saveCornerRadius(s.cornerRadius);
    saveShadowEnabled(s.shadowEnabled);
    saveShadowBlur(s.shadowBlur);
    saveShadowOffsetX(s.shadowOffsetX);
    saveShadowOffsetY(s.shadowOffsetY);
    saveShadowColor(s.shadowColor);
    saveAspectRatio(s.aspectRatio);
}

BeautifyBackgroundType BeautifySettingsManager::loadBackgroundType() const
{
    auto settings = SnapTray::getSettings();
    int val = settings.value(kKeyBackgroundType, static_cast<int>(BeautifyBackgroundType::LinearGradient)).toInt();
    if (val < 0 || val > 2) val = static_cast<int>(BeautifyBackgroundType::LinearGradient);
    return static_cast<BeautifyBackgroundType>(val);
}

void BeautifySettingsManager::saveBackgroundType(BeautifyBackgroundType type)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyBackgroundType, static_cast<int>(type));
}

QColor BeautifySettingsManager::loadBackgroundColor() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyBackgroundColor, BeautifySettings{}.backgroundColor).value<QColor>();
}

void BeautifySettingsManager::saveBackgroundColor(const QColor& color)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyBackgroundColor, color);
}

QColor BeautifySettingsManager::loadGradientStartColor() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyGradientStart, BeautifySettings{}.gradientStartColor).value<QColor>();
}

void BeautifySettingsManager::saveGradientStartColor(const QColor& color)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyGradientStart, color);
}

QColor BeautifySettingsManager::loadGradientEndColor() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyGradientEnd, BeautifySettings{}.gradientEndColor).value<QColor>();
}

void BeautifySettingsManager::saveGradientEndColor(const QColor& color)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyGradientEnd, color);
}

int BeautifySettingsManager::loadGradientAngle() const
{
    auto settings = SnapTray::getSettings();
    int angle = settings.value(kKeyGradientAngle, 135).toInt();
    return std::clamp(angle, 0, 360);
}

void BeautifySettingsManager::saveGradientAngle(int angle)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyGradientAngle, angle);
}

int BeautifySettingsManager::loadPadding() const
{
    auto settings = SnapTray::getSettings();
    int padding = settings.value(kKeyPadding, 64).toInt();
    return std::clamp(padding, 16, 200);
}

void BeautifySettingsManager::savePadding(int padding)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyPadding, padding);
}

int BeautifySettingsManager::loadCornerRadius() const
{
    auto settings = SnapTray::getSettings();
    int radius = settings.value(kKeyCornerRadius, 12).toInt();
    return std::clamp(radius, 0, 40);
}

void BeautifySettingsManager::saveCornerRadius(int radius)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyCornerRadius, radius);
}

bool BeautifySettingsManager::loadShadowEnabled() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyShadowEnabled, true).toBool();
}

void BeautifySettingsManager::saveShadowEnabled(bool enabled)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyShadowEnabled, enabled);
}

int BeautifySettingsManager::loadShadowBlur() const
{
    auto settings = SnapTray::getSettings();
    int blur = settings.value(kKeyShadowBlur, 40).toInt();
    return std::clamp(blur, 0, 100);
}

void BeautifySettingsManager::saveShadowBlur(int blur)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyShadowBlur, blur);
}

QColor BeautifySettingsManager::loadShadowColor() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kKeyShadowColor, BeautifySettings{}.shadowColor).value<QColor>();
}

void BeautifySettingsManager::saveShadowColor(const QColor& color)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyShadowColor, color);
}

int BeautifySettingsManager::loadShadowOffsetX() const
{
    auto settings = SnapTray::getSettings();
    int offset = settings.value(kKeyShadowOffsetX, 0).toInt();
    return std::clamp(offset, -50, 50);
}

void BeautifySettingsManager::saveShadowOffsetX(int offset)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyShadowOffsetX, offset);
}

int BeautifySettingsManager::loadShadowOffsetY() const
{
    auto settings = SnapTray::getSettings();
    int offset = settings.value(kKeyShadowOffsetY, 8).toInt();
    return std::clamp(offset, -50, 50);
}

void BeautifySettingsManager::saveShadowOffsetY(int offset)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyShadowOffsetY, offset);
}

BeautifyAspectRatio BeautifySettingsManager::loadAspectRatio() const
{
    auto settings = SnapTray::getSettings();
    int val = settings.value(kKeyAspectRatio, static_cast<int>(BeautifyAspectRatio::Auto)).toInt();
    if (val < 0 || val > 5) val = 0;
    return static_cast<BeautifyAspectRatio>(val);
}

void BeautifySettingsManager::saveAspectRatio(BeautifyAspectRatio ratio)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kKeyAspectRatio, static_cast<int>(ratio));
}
