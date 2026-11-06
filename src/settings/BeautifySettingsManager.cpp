#include "settings/BeautifySettingsManager.h"
#include "settings/Settings.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    {
        auto stored = SnapTray::getSettings();
        const BeautifySettings d;
        s.borderEnabled = stored.value(kKeyBorderEnabled, d.borderEnabled).toBool();
        s.borderWidth = std::clamp(stored.value(kKeyBorderWidth, d.borderWidth).toInt(), kMinBorderWidth, kMaxBorderWidth);
        const QColor borderColor = stored.value(kKeyBorderColor, d.borderColor).value<QColor>();
        s.borderColor = borderColor.isValid() ? borderColor : d.borderColor;
    }
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
    auto stored = SnapTray::getSettings();
    stored.setValue(kKeyBorderEnabled, s.borderEnabled);
    stored.setValue(kKeyBorderWidth, s.borderWidth);
    stored.setValue(kKeyBorderColor, s.borderColor);
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

namespace {

QColor colorOr(const QJsonObject& json, const char* key, const QColor& fallback)
{
    const QColor color(json.value(QLatin1String(key)).toString());
    return color.isValid() ? color : fallback;
}

// A style saved before #269: pixel values, clamped like the single settings.
BeautifySettings legacyStyleFromJson(const QJsonObject& json)
{
    const BeautifySettings d;
    BeautifySettings s;
    s.backgroundType = static_cast<BeautifyBackgroundType>(
        std::clamp(json.value(QStringLiteral("backgroundType")).toInt(static_cast<int>(d.backgroundType)), 0, 2));
    s.backgroundColor = colorOr(json, "backgroundColor", d.backgroundColor);
    s.gradientStartColor = colorOr(json, "gradientStartColor", d.gradientStartColor);
    s.gradientEndColor = colorOr(json, "gradientEndColor", d.gradientEndColor);
    s.gradientAngle = std::clamp(json.value(QStringLiteral("gradientAngle")).toInt(d.gradientAngle), 0, 360);
    s.padding = std::clamp(json.value(QStringLiteral("padding")).toInt(d.padding), 16, 200);
    s.cornerRadius = std::clamp(json.value(QStringLiteral("cornerRadius")).toInt(d.cornerRadius), 0, 40);
    s.shadowEnabled = json.value(QStringLiteral("shadowEnabled")).toBool(d.shadowEnabled);
    s.shadowBlur = std::clamp(json.value(QStringLiteral("shadowBlur")).toInt(d.shadowBlur), 0, 100);
    s.shadowOffsetX = std::clamp(json.value(QStringLiteral("shadowOffsetX")).toInt(d.shadowOffsetX), -50, 50);
    s.shadowOffsetY = std::clamp(json.value(QStringLiteral("shadowOffsetY")).toInt(d.shadowOffsetY), -50, 50);
    s.shadowColor = colorOr(json, "shadowColor", d.shadowColor);
    s.aspectRatio = static_cast<BeautifyAspectRatio>(
        std::clamp(json.value(QStringLiteral("aspectRatio")).toInt(static_cast<int>(d.aspectRatio)), 0, 5));
    return s;
}

QJsonArray readArray(const QSettings& settings, const char* key)
{
    return QJsonDocument::fromJson(settings.value(QLatin1String(key)).toString().toUtf8()).array();
}

} // namespace

QVector<SnapTray::NamedFrameStyle> BeautifySettingsManager::loadFrameStyles() const
{
    auto settings = SnapTray::getSettings();
    QVector<SnapTray::NamedFrameStyle> styles;
    const auto add = [&styles](const QString& name, const SnapTray::FrameStyle& style) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty() && styles.size() < kMaxCustomStyles) {
            styles.append({trimmed, style});
        }
    };
    if (settings.contains(QLatin1String(kKeyFrameStyles))) {
        for (const QJsonValue& value : readArray(settings, kKeyFrameStyles)) {
            const QJsonObject json = value.toObject();
            add(json.value(QStringLiteral("name")).toString(),
                SnapTray::FrameStyle::fromJson(json.value(QStringLiteral("style")).toObject()));
        }
        return styles;
    }
    for (const QJsonValue& value : readArray(settings, kKeyCustomStyles)) {
        const QJsonObject json = value.toObject();
        add(json.value(QStringLiteral("name")).toString(),
            SnapTray::FrameStyle::fromBeautify(legacyStyleFromJson(json)));
    }
    return styles;
}

void BeautifySettingsManager::saveFrameStyles(const QVector<SnapTray::NamedFrameStyle>& styles)
{
    QJsonArray array;
    for (const auto& style : styles) {
        const QString name = style.name.trimmed();
        if (!name.isEmpty() && array.size() < kMaxCustomStyles) {
            array.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("style"), style.style.toJson()}});
        }
    }
    // Kept even when empty: a missing key would bring the old styles back.
    auto settings = SnapTray::getSettings();
    settings.setValue(QLatin1String(kKeyFrameStyles),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}
