#include "beautify/FrameStyle.h"

#include <algorithm>
#include <cmath>

namespace SnapTray {

namespace {

// Fractions are compared to this many decimals, so a style read back from
// JSON still matches the one it was written from.
constexpr double kFractionTolerance = 1e-6;
constexpr int kMaxAngle = 360;
constexpr int kMaxAspectRatio = int(BeautifyAspectRatio::Twitter_2_1);

struct NamedBackground {
    FrameStyle::Background value;
    const char* name;
};

constexpr NamedBackground kBackgrounds[] = {
    {FrameStyle::Background::Solid, "solid"},
    {FrameStyle::Background::LinearGradient, "linearGradient"},
    {FrameStyle::Background::RadialGradient, "radialGradient"},
};

struct BackgroundMapping {
    FrameStyle::Background style;
    BeautifyBackgroundType beautify;
};

constexpr BackgroundMapping kBackgroundMappings[] = {
    {FrameStyle::Background::Solid, BeautifyBackgroundType::Solid},
    {FrameStyle::Background::LinearGradient, BeautifyBackgroundType::LinearGradient},
    {FrameStyle::Background::RadialGradient, BeautifyBackgroundType::RadialGradient},
};

BeautifyBackgroundType toBeautifyBackground(FrameStyle::Background background)
{
    for (const auto& m : kBackgroundMappings) {
        if (m.style == background) return m.beautify;
    }
    return BeautifyBackgroundType::LinearGradient;
}

FrameStyle::Background fromBeautifyBackground(BeautifyBackgroundType type)
{
    for (const auto& m : kBackgroundMappings) {
        if (m.beautify == type) return m.style;
    }
    return FrameStyle::Background::LinearGradient;
}

bool sameFraction(double a, double b)
{
    return std::abs(a - b) < kFractionTolerance;
}

int pixels(double fraction, int shortSide)
{
    return int(std::lround(fraction * shortSide));
}

double fraction(int pixels, int shortSide)
{
    return double(pixels) / std::max(1, shortSide);
}

QColor colorOr(const QJsonObject& json, const char* key, const QColor& fallback)
{
    const QColor color(json.value(QLatin1String(key)).toString());
    return color.isValid() ? color : fallback;
}

double fractionOr(const QJsonObject& json, const char* key, double fallback, double low = 0.0)
{
    const QJsonValue value = json.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return fallback;
    return std::clamp(value.toDouble(), low, FrameStyle::kMaxSizeFraction);
}

} // namespace

bool FrameStyle::operator==(const FrameStyle& o) const
{
    return background == o.background && solidColor == o.solidColor && gradientStart == o.gradientStart
        && gradientEnd == o.gradientEnd && gradientAngle == o.gradientAngle && sameFraction(padding, o.padding)
        && sameFraction(cornerRadius, o.cornerRadius) && shadowEnabled == o.shadowEnabled
        && sameFraction(shadowBlur, o.shadowBlur) && sameFraction(shadowOffsetX, o.shadowOffsetX)
        && sameFraction(shadowOffsetY, o.shadowOffsetY) && shadowColor == o.shadowColor
        && borderEnabled == o.borderEnabled && sameFraction(borderWidth, o.borderWidth)
        && borderColor == o.borderColor && aspectRatio == o.aspectRatio && deviceFrame == o.deviceFrame;
}

BeautifySettings FrameStyle::toBeautify(int shortSide) const
{
    const int side = std::max(1, shortSide);
    BeautifySettings s;
    s.backgroundType = toBeautifyBackground(background);
    s.backgroundColor = solidColor;
    s.gradientStartColor = gradientStart;
    s.gradientEndColor = gradientEnd;
    s.gradientAngle = gradientAngle;
    s.padding = pixels(padding, side);
    s.cornerRadius = pixels(cornerRadius, side);
    s.shadowEnabled = shadowEnabled;
    s.shadowBlur = pixels(shadowBlur, side);
    s.shadowOffsetX = pixels(shadowOffsetX, side);
    s.shadowOffsetY = pixels(shadowOffsetY, side);
    s.shadowColor = shadowColor;
    s.borderEnabled = borderEnabled;
    // A border that is switched on is always visible.
    s.borderWidth = std::max(1, pixels(borderWidth, side));
    s.borderColor = borderColor;
    s.aspectRatio = aspectRatio;
    return s;
}

FrameStyle FrameStyle::fromBeautify(const BeautifySettings& settings, int shortSide)
{
    const int side = std::max(1, shortSide);
    FrameStyle f;
    f.background = fromBeautifyBackground(settings.backgroundType);
    f.solidColor = settings.backgroundColor;
    f.gradientStart = settings.gradientStartColor;
    f.gradientEnd = settings.gradientEndColor;
    f.gradientAngle = settings.gradientAngle;
    f.padding = fraction(settings.padding, side);
    f.cornerRadius = fraction(settings.cornerRadius, side);
    f.shadowEnabled = settings.shadowEnabled;
    f.shadowBlur = fraction(settings.shadowBlur, side);
    f.shadowOffsetX = fraction(settings.shadowOffsetX, side);
    f.shadowOffsetY = fraction(settings.shadowOffsetY, side);
    f.shadowColor = settings.shadowColor;
    f.borderEnabled = settings.borderEnabled;
    f.borderWidth = fraction(settings.borderWidth, side);
    f.borderColor = settings.borderColor;
    f.aspectRatio = settings.aspectRatio;
    return f;
}

QJsonObject FrameStyle::toJson() const
{
    const char* backgroundName = kBackgrounds[0].name;
    for (const auto& entry : kBackgrounds) {
        if (entry.value == background) backgroundName = entry.name;
    }
    QJsonObject json{
        {QStringLiteral("background"), QLatin1String(backgroundName)},
        {QStringLiteral("solidColor"), solidColor.name(QColor::HexArgb)},
        {QStringLiteral("gradientStart"), gradientStart.name(QColor::HexArgb)},
        {QStringLiteral("gradientEnd"), gradientEnd.name(QColor::HexArgb)},
        {QStringLiteral("gradientAngle"), gradientAngle},
        {QStringLiteral("padding"), padding},
        {QStringLiteral("cornerRadius"), cornerRadius},
        {QStringLiteral("shadowEnabled"), shadowEnabled},
        {QStringLiteral("shadowBlur"), shadowBlur},
        {QStringLiteral("shadowOffsetX"), shadowOffsetX},
        {QStringLiteral("shadowOffsetY"), shadowOffsetY},
        {QStringLiteral("shadowColor"), shadowColor.name(QColor::HexArgb)},
        {QStringLiteral("borderEnabled"), borderEnabled},
        {QStringLiteral("borderWidth"), borderWidth},
        {QStringLiteral("borderColor"), borderColor.name(QColor::HexArgb)},
        {QStringLiteral("aspectRatio"), int(aspectRatio)},
    };
    if (!deviceFrame.isEmpty()) json.insert(QStringLiteral("deviceFrame"), deviceFrame);
    return json;
}

FrameStyle FrameStyle::fromJson(const QJsonObject& json)
{
    FrameStyle f;
    const QString backgroundName = json.value(QStringLiteral("background")).toString();
    for (const auto& entry : kBackgrounds) {
        if (backgroundName == QLatin1String(entry.name)) f.background = entry.value;
    }
    f.solidColor = colorOr(json, "solidColor", f.solidColor);
    f.gradientStart = colorOr(json, "gradientStart", f.gradientStart);
    f.gradientEnd = colorOr(json, "gradientEnd", f.gradientEnd);
    f.gradientAngle = std::clamp(json.value(QStringLiteral("gradientAngle")).toInt(f.gradientAngle), 0, kMaxAngle);
    f.padding = fractionOr(json, "padding", f.padding);
    f.cornerRadius = fractionOr(json, "cornerRadius", f.cornerRadius);
    f.shadowEnabled = json.value(QStringLiteral("shadowEnabled")).toBool(f.shadowEnabled);
    f.shadowBlur = fractionOr(json, "shadowBlur", f.shadowBlur);
    f.shadowOffsetX = fractionOr(json, "shadowOffsetX", f.shadowOffsetX, -FrameStyle::kMaxSizeFraction);
    f.shadowOffsetY = fractionOr(json, "shadowOffsetY", f.shadowOffsetY, -FrameStyle::kMaxSizeFraction);
    f.shadowColor = colorOr(json, "shadowColor", f.shadowColor);
    f.borderEnabled = json.value(QStringLiteral("borderEnabled")).toBool(f.borderEnabled);
    f.borderWidth = fractionOr(json, "borderWidth", f.borderWidth);
    f.borderColor = colorOr(json, "borderColor", f.borderColor);
    const int ratio = json.value(QStringLiteral("aspectRatio")).toInt(int(f.aspectRatio));
    if (ratio >= 0 && ratio <= kMaxAspectRatio) f.aspectRatio = BeautifyAspectRatio(ratio);
    f.deviceFrame = json.value(QStringLiteral("deviceFrame")).toString();
    return f;
}

} // namespace SnapTray
