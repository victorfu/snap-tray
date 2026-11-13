#include "beautify/BeautifySettingsJson.h"

#include <QJsonValue>
#include <QLatin1String>

#include <cmath>

namespace {

// Limits for stored values. They are far above what the Beautify panel offers
// and only keep a damaged manifest from asking for an absurd picture.
constexpr int kArgbNameLength = 9; // "#AARRGGBB"
constexpr int kMaxAngle = 360;
constexpr int kMaxSizePixels = 4096;
constexpr int kMaxBackgroundType = static_cast<int>(BeautifyBackgroundType::RadialGradient);
constexpr int kMaxAspectRatio = static_cast<int>(BeautifyAspectRatio::Twitter_2_1);

bool readInt(const QJsonObject& json, const char* key, int minimum, int maximum, int* out)
{
    const QJsonValue value = json.value(QLatin1String(key));
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (std::floor(number) != number || number < minimum || number > maximum) {
        return false;
    }
    *out = static_cast<int>(number);
    return true;
}

bool readBool(const QJsonObject& json, const char* key, bool* out)
{
    const QJsonValue value = json.value(QLatin1String(key));
    if (!value.isBool()) {
        return false;
    }
    *out = value.toBool();
    return true;
}

bool readColor(const QJsonObject& json, const char* key, QColor* out)
{
    const QJsonValue value = json.value(QLatin1String(key));
    if (!value.isString()) {
        return false;
    }
    const QString name = value.toString();
    // Only the "#AARRGGBB" form written below; QColor would also take names.
    if (name.size() != kArgbNameLength || !name.startsWith(QLatin1Char('#'))) {
        return false;
    }
    const QColor color = QColor::fromString(name);
    if (!color.isValid()) {
        return false;
    }
    *out = color;
    return true;
}

QString colorName(const QColor& color)
{
    return color.toRgb().name(QColor::HexArgb);
}

} // namespace

namespace SnapTray {

QJsonObject beautifySettingsToJson(const BeautifySettings& settings)
{
    return QJsonObject{
        {QStringLiteral("backgroundType"), static_cast<int>(settings.backgroundType)},
        {QStringLiteral("backgroundColor"), colorName(settings.backgroundColor)},
        {QStringLiteral("gradientStartColor"), colorName(settings.gradientStartColor)},
        {QStringLiteral("gradientEndColor"), colorName(settings.gradientEndColor)},
        {QStringLiteral("gradientAngle"), settings.gradientAngle},
        {QStringLiteral("padding"), settings.padding},
        {QStringLiteral("cornerRadius"), settings.cornerRadius},
        {QStringLiteral("shadowEnabled"), settings.shadowEnabled},
        {QStringLiteral("shadowBlur"), settings.shadowBlur},
        {QStringLiteral("shadowOffsetX"), settings.shadowOffsetX},
        {QStringLiteral("shadowOffsetY"), settings.shadowOffsetY},
        {QStringLiteral("shadowColor"), colorName(settings.shadowColor)},
        {QStringLiteral("borderEnabled"), settings.borderEnabled},
        {QStringLiteral("borderWidth"), settings.borderWidth},
        {QStringLiteral("borderColor"), colorName(settings.borderColor)},
        {QStringLiteral("aspectRatio"), static_cast<int>(settings.aspectRatio)},
    };
}

std::optional<BeautifySettings> beautifySettingsFromJson(const QJsonObject& json)
{
    BeautifySettings s;
    int backgroundType = 0;
    int aspectRatio = 0;
    const bool ok =
        readInt(json, "backgroundType", 0, kMaxBackgroundType, &backgroundType) &&
        readColor(json, "backgroundColor", &s.backgroundColor) &&
        readColor(json, "gradientStartColor", &s.gradientStartColor) &&
        readColor(json, "gradientEndColor", &s.gradientEndColor) &&
        readInt(json, "gradientAngle", 0, kMaxAngle, &s.gradientAngle) &&
        readInt(json, "padding", 0, kMaxSizePixels, &s.padding) &&
        readInt(json, "cornerRadius", 0, kMaxSizePixels, &s.cornerRadius) &&
        readBool(json, "shadowEnabled", &s.shadowEnabled) &&
        readInt(json, "shadowBlur", 0, kMaxSizePixels, &s.shadowBlur) &&
        readInt(json, "shadowOffsetX", -kMaxSizePixels, kMaxSizePixels, &s.shadowOffsetX) &&
        readInt(json, "shadowOffsetY", -kMaxSizePixels, kMaxSizePixels, &s.shadowOffsetY) &&
        readColor(json, "shadowColor", &s.shadowColor) &&
        readBool(json, "borderEnabled", &s.borderEnabled) &&
        readInt(json, "borderWidth", 0, kMaxSizePixels, &s.borderWidth) &&
        readColor(json, "borderColor", &s.borderColor) &&
        readInt(json, "aspectRatio", 0, kMaxAspectRatio, &aspectRatio);
    if (!ok) {
        return std::nullopt;
    }
    s.backgroundType = static_cast<BeautifyBackgroundType>(backgroundType);
    s.aspectRatio = static_cast<BeautifyAspectRatio>(aspectRatio);
    return s;
}

} // namespace SnapTray
