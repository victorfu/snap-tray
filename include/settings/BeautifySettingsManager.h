#ifndef BEAUTIFYSETTINGSMANAGER_H
#define BEAUTIFYSETTINGSMANAGER_H

#include "beautify/BeautifySettings.h"

class BeautifySettingsManager {
public:
    static BeautifySettingsManager& instance();

    BeautifySettings loadSettings() const;
    void saveSettings(const BeautifySettings& settings);

    BeautifyBackgroundType loadBackgroundType() const;
    void saveBackgroundType(BeautifyBackgroundType type);

    QColor loadBackgroundColor() const;
    void saveBackgroundColor(const QColor& color);

    QColor loadGradientStartColor() const;
    void saveGradientStartColor(const QColor& color);

    QColor loadGradientEndColor() const;
    void saveGradientEndColor(const QColor& color);

    int loadGradientAngle() const;
    void saveGradientAngle(int angle);

    int loadPadding() const;
    void savePadding(int padding);

    int loadCornerRadius() const;
    void saveCornerRadius(int radius);

    bool loadShadowEnabled() const;
    void saveShadowEnabled(bool enabled);

    int loadShadowBlur() const;
    void saveShadowBlur(int blur);

    QColor loadShadowColor() const;
    void saveShadowColor(const QColor& color);

    int loadShadowOffsetX() const;
    void saveShadowOffsetX(int offset);

    int loadShadowOffsetY() const;
    void saveShadowOffsetY(int offset);

    BeautifyAspectRatio loadAspectRatio() const;
    void saveAspectRatio(BeautifyAspectRatio ratio);

private:
    BeautifySettingsManager() = default;
    BeautifySettingsManager(const BeautifySettingsManager&) = delete;
    BeautifySettingsManager& operator=(const BeautifySettingsManager&) = delete;

    static constexpr const char* kKeyBackgroundType     = "beautify/backgroundType";
    static constexpr const char* kKeyBackgroundColor    = "beautify/backgroundColor";
    static constexpr const char* kKeyGradientStart      = "beautify/gradientStartColor";
    static constexpr const char* kKeyGradientEnd        = "beautify/gradientEndColor";
    static constexpr const char* kKeyGradientAngle      = "beautify/gradientAngle";
    static constexpr const char* kKeyPadding            = "beautify/padding";
    static constexpr const char* kKeyCornerRadius       = "beautify/cornerRadius";
    static constexpr const char* kKeyShadowEnabled      = "beautify/shadowEnabled";
    static constexpr const char* kKeyShadowBlur         = "beautify/shadowBlur";
    static constexpr const char* kKeyShadowOffsetX      = "beautify/shadowOffsetX";
    static constexpr const char* kKeyShadowOffsetY      = "beautify/shadowOffsetY";
    static constexpr const char* kKeyShadowColor        = "beautify/shadowColor";
    static constexpr const char* kKeyAspectRatio        = "beautify/aspectRatio";
};

#endif // BEAUTIFYSETTINGSMANAGER_H
