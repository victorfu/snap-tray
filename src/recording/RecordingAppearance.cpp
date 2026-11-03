#include "recording/RecordingAppearance.h"

#include "beautify/BeautifyRenderer.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace SnapTray {

const QColor RecordingAppearance::kBorderColor(255, 255, 255, 170);

namespace {

// The blurred background is the picture shrunk this many times and scaled
// back up, then dimmed so the picture stands out from it.
constexpr int kBlurReduction = 24;
constexpr int kBlurDimAlpha = 50;
constexpr int kMaxAspectRatio = int(BeautifyAspectRatio::Twitter_2_1);

template <typename Enum>
struct Named {
    Enum value;
    const char* name;
};

constexpr Named<RecordingAppearance::Background> kBackgrounds[] = {
    {RecordingAppearance::Background::Gradient, "gradient"},
    {RecordingAppearance::Background::Solid, "solid"},
    {RecordingAppearance::Background::BlurredVideo, "blurredVideo"},
    {RecordingAppearance::Background::Image, "image"},
};

constexpr Named<RecordingAppearance::Amount> kAmounts[] = {
    {RecordingAppearance::Amount::None, "none"},
    {RecordingAppearance::Amount::Small, "small"},
    {RecordingAppearance::Amount::Medium, "medium"},
    {RecordingAppearance::Amount::Large, "large"},
};

template <typename Enum, size_t N>
const char* nameOf(const Named<Enum> (&table)[N], Enum value)
{
    for (const auto& entry : table) {
        if (entry.value == value) return entry.name;
    }
    return table[0].name;
}

template <typename Enum, size_t N>
Enum valueOf(const Named<Enum> (&table)[N], const QString& name, Enum fallback)
{
    for (const auto& entry : table) {
        if (name == QLatin1String(entry.name)) return entry.value;
    }
    return fallback;
}

int shortSide(const QSize& size)
{
    return std::max(1, std::min(size.width(), size.height()));
}

int evenUp(int value)
{
    return value + (value & 1);
}

// Scaled to cover `target`, centred, like a desktop wallpaper.
void drawCover(QPainter& painter, const QRect& target, const QImage& image)
{
    if (image.isNull() || target.isEmpty()) return;
    const double scale = std::max(double(target.width()) / image.width(), double(target.height()) / image.height());
    const QSizeF shown(image.width() * scale, image.height() * scale);
    const QRectF at(target.x() + (target.width() - shown.width()) / 2.0,
                    target.y() + (target.height() - shown.height()) / 2.0, shown.width(), shown.height());
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipRect(target);
    painter.drawImage(at, image);
    painter.restore();
}

} // namespace

bool RecordingAppearance::operator==(const RecordingAppearance& o) const
{
    return enabled == o.enabled && background == o.background && gradientPreset == o.gradientPreset
        && solidColor == o.solidColor && imagePath == o.imagePath && padding == o.padding && corners == o.corners
        && shadow == o.shadow && border == o.border && aspectRatio == o.aspectRatio;
}

QJsonObject RecordingAppearance::toJson() const
{
    return {{QStringLiteral("enabled"), enabled},
            {QStringLiteral("background"), QLatin1String(nameOf(kBackgrounds, background))},
            {QStringLiteral("gradientPreset"), gradientPreset},
            {QStringLiteral("solidColor"), solidColor.name(QColor::HexRgb)},
            {QStringLiteral("imagePath"), imagePath},
            {QStringLiteral("padding"), QLatin1String(nameOf(kAmounts, padding))},
            {QStringLiteral("corners"), QLatin1String(nameOf(kAmounts, corners))},
            {QStringLiteral("shadow"), shadow},
            {QStringLiteral("border"), border},
            {QStringLiteral("aspectRatio"), int(aspectRatio)}};
}

RecordingAppearance RecordingAppearance::fromJson(const QJsonObject& json)
{
    RecordingAppearance a;
    a.enabled = json.value(QStringLiteral("enabled")).toBool(a.enabled);
    a.background = valueOf(kBackgrounds, json.value(QStringLiteral("background")).toString(), a.background);
    const int preset = json.value(QStringLiteral("gradientPreset")).toInt(a.gradientPreset);
    if (preset >= 0 && preset < beautifyPresets().size()) a.gradientPreset = preset;
    const QColor solid(json.value(QStringLiteral("solidColor")).toString());
    if (solid.isValid()) a.solidColor = solid;
    a.imagePath = json.value(QStringLiteral("imagePath")).toString();
    a.padding = valueOf(kAmounts, json.value(QStringLiteral("padding")).toString(), a.padding);
    a.corners = valueOf(kAmounts, json.value(QStringLiteral("corners")).toString(), a.corners);
    a.shadow = json.value(QStringLiteral("shadow")).toBool(a.shadow);
    a.border = json.value(QStringLiteral("border")).toBool(a.border);
    const int ratio = json.value(QStringLiteral("aspectRatio")).toInt(int(a.aspectRatio));
    if (ratio >= 0 && ratio <= kMaxAspectRatio) a.aspectRatio = BeautifyAspectRatio(ratio);
    return a;
}

RecordingAppearance RecordingAppearance::fromBeautify(const BeautifySettings& settings)
{
    RecordingAppearance a;
    if (settings.backgroundType == BeautifyBackgroundType::Solid) {
        a.background = Background::Solid;
        // Screenshot Beautify allows a translucent color; a video frame has
        // nothing behind it, so the frame's color is always opaque (PR #198 review).
        a.solidColor = settings.backgroundColor.toRgb();
        a.solidColor.setAlpha(255);
    }
    const auto presets = beautifyPresets();
    for (int i = 0; i < presets.size(); ++i) {
        if (presets[i].gradientStart == settings.gradientStartColor && presets[i].gradientEnd == settings.gradientEndColor) {
            a.gradientPreset = i;
        }
    }
    a.shadow = settings.shadowEnabled;
    a.corners = settings.cornerRadius <= 0 ? Amount::None : Amount::Medium;
    a.aspectRatio = settings.aspectRatio;
    return a;
}

BeautifySettings RecordingAppearance::beautifyFor(const QSize& contentSize) const
{
    const int side = shortSide(contentSize);
    BeautifySettings s;
    const auto presets = beautifyPresets();
    const BeautifyPreset& preset = presets.at(std::clamp(gradientPreset, 0, int(presets.size()) - 1));
    s.backgroundType = background == Background::Solid ? BeautifyBackgroundType::Solid
                                                       : BeautifyBackgroundType::LinearGradient;
    s.backgroundColor = solidColor;
    s.gradientStartColor = preset.gradientStart;
    s.gradientEndColor = preset.gradientEnd;
    s.gradientAngle = preset.gradientAngle;
    s.padding = int(std::lround(side * kPaddingFractions[int(padding)]));
    s.cornerRadius = int(std::lround(side * kCornerFractions[int(corners)]));
    s.shadowEnabled = shadow;
    s.shadowBlur = std::max(1, int(std::lround(side * kShadowBlurFraction)));
    s.shadowOffsetX = 0;
    s.shadowOffsetY = int(std::lround(side * kShadowOffsetFraction));
    s.aspectRatio = aspectRatio;
    return s;
}

QSize RecordingAppearance::outputSize(const QSize& contentSize) const
{
    if (!enabled || contentSize.isEmpty()) return contentSize;
    const QSize framed = BeautifyRenderer::calculateOutputSize(contentSize, beautifyFor(contentSize));
    return QSize(evenUp(std::max(framed.width(), contentSize.width())),
                 evenUp(std::max(framed.height(), contentSize.height())));
}

QRect RecordingAppearance::contentRect(const QSize& contentSize) const
{
    if (!enabled || contentSize.isEmpty()) return QRect(QPoint(0, 0), contentSize);
    return BeautifyRenderer::calculateInsetRect(outputSize(contentSize), contentSize, beautifyFor(contentSize));
}

int RecordingAppearance::borderWidth(const QSize& contentSize) const
{
    return border ? std::max(1, int(std::lround(shortSide(contentSize) * kBorderFraction))) : 0;
}

double RecordingAppearance::cornerRadius(const QSize& contentSize) const
{
    return beautifyFor(contentSize).cornerRadius;
}

RecordingAppearanceRenderer::RecordingAppearanceRenderer(const RecordingAppearance& appearance,
                                                         const QSize& contentSize, const QImage& backgroundImage,
                                                         bool cacheLayers)
    : m_appearance(appearance)
    , m_settings(appearance.beautifyFor(contentSize))
    , m_contentSize(contentSize)
    , m_outputSize(appearance.outputSize(contentSize))
    , m_contentRect(appearance.contentRect(contentSize))
    , m_backgroundImage(backgroundImage)
{
    if (!m_appearance.enabled || contentSize.isEmpty() || !cacheLayers) return;
    if (m_settings.cornerRadius > 0) {
        m_cornerMask = QImage(contentSize, QImage::Format_ARGB32_Premultiplied);
        m_cornerMask.fill(Qt::transparent);
        QPainter painter(&m_cornerMask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(contentSize)), m_settings.cornerRadius, m_settings.cornerRadius);
        painter.fillPath(path, Qt::white);
    }
    if (m_appearance.background != RecordingAppearance::Background::BlurredVideo) {
        m_staticLayer = QImage(m_outputSize, QImage::Format_RGB32);
        QPainter painter(&m_staticLayer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        drawBackground(painter, QImage());
        drawDecorations(painter);
    }
}

void RecordingAppearanceRenderer::drawBackground(QPainter& painter, const QImage& blurSource) const
{
    const QRect all(QPoint(0, 0), m_outputSize);
    // An opaque base under every background: a transparent image or a
    // translucent color must not show whatever the new frame's memory held
    // (PR #198 review).
    painter.fillRect(all, Qt::black);
    if (m_appearance.background == RecordingAppearance::Background::Image && !m_backgroundImage.isNull()) {
        drawCover(painter, all, m_backgroundImage);
        return;
    }
    if (m_appearance.background == RecordingAppearance::Background::BlurredVideo && !blurSource.isNull()) {
        // Scaling the small copy back up is the blur.
        drawCover(painter, all, blurSource);
        painter.fillRect(all, QColor(0, 0, 0, kBlurDimAlpha));
        return;
    }
    // Solid or gradient; a missing image or picture falls back to the gradient.
    BeautifySettings settings = m_settings;
    if (m_appearance.background != RecordingAppearance::Background::Solid) {
        settings.backgroundType = BeautifyBackgroundType::LinearGradient;
    }
    BeautifyRenderer::drawBackground(painter, all, settings);
}

void RecordingAppearanceRenderer::drawDecorations(QPainter& painter) const
{
    BeautifyRenderer::drawShadow(painter, m_contentRect, m_settings);
}

void RecordingAppearanceRenderer::drawBorder(QPainter& painter) const
{
    const int width = m_appearance.borderWidth(m_contentSize);
    if (width <= 0) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Inside the picture's edge, so the frame size does not change.
    const QRectF edge = QRectF(m_contentRect).adjusted(width / 2.0, width / 2.0, -width / 2.0, -width / 2.0);
    const double radius = std::max(0.0, m_settings.cornerRadius - width / 2.0);
    painter.setPen(QPen(RecordingAppearance::kBorderColor, width));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(edge, radius, radius);
    painter.restore();
}

QImage RecordingAppearanceRenderer::compose(const QImage& content, const QImage& blurSource) const
{
    if (!m_appearance.enabled || content.isNull()) return content;
    QImage out;
    if (!m_staticLayer.isNull()) {
        out = m_staticLayer.copy();
    } else {
        out = QImage(m_outputSize, QImage::Format_RGB32);
        QPainter background(&out);
        background.setRenderHint(QPainter::Antialiasing, true);
        drawBackground(background, blurSource.isNull() ? blurSourceFor(content) : blurSource);
        drawDecorations(background);
    }
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QImage picture = content.size() == m_contentSize
        ? content
        : content.scaled(m_contentSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (m_cornerMask.isNull()) {
        painter.drawImage(m_contentRect.topLeft(), picture);
    } else {
        QImage rounded = picture.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter mask(&rounded);
        mask.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        mask.drawImage(0, 0, m_cornerMask);
        mask.end();
        painter.drawImage(m_contentRect.topLeft(), rounded);
    }
    drawBorder(painter);
    painter.end();
    return out;
}

QImage RecordingAppearanceRenderer::blurSourceFor(const QImage& picture)
{
    if (picture.isNull()) return picture;
    return picture.scaled(std::max(1, picture.width() / kBlurReduction), std::max(1, picture.height() / kBlurReduction),
                          Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void RecordingAppearanceRenderer::paintAround(QPainter& painter, const QRectF& contentInView,
                                              const QImage& blurSource) const
{
    if (!m_appearance.enabled || m_contentSize.isEmpty() || contentInView.isEmpty()) return;
    const double scale = contentInView.width() / m_contentSize.width();
    painter.save();
    painter.translate(contentInView.topLeft() - QPointF(m_contentRect.topLeft()) * scale);
    painter.scale(scale, scale);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    drawBackground(painter, blurSource);
    drawDecorations(painter);
    // Leave the picture's rounded shape clear: the video shows through.
    QPainterPath shape;
    shape.addRoundedRect(QRectF(m_contentRect), m_settings.cornerRadius, m_settings.cornerRadius);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillPath(shape, Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    drawBorder(painter);
    painter.restore();
}

} // namespace SnapTray
