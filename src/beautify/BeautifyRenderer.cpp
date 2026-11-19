#include "beautify/BeautifyRenderer.h"
#include "utils/CoordinateHelper.h"
#include "beautify/BeautifyBackgroundImage.h"
#include <QColorSpace>
#include <QPainterPath>
#include <QPen>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QDebug>
#include <cmath>
#include <map>

namespace {

void paintSource(QPainter& painter, const QRect& target, const QPixmap& source)
{
    painter.drawPixmap(target, source);
}

void paintSource(QPainter& painter, const QRect& target, const QImage& source)
{
    painter.drawImage(target, source);
}

// Cover the canvas with a centered crop. All coordinates refer to image
// pixels, independently of the source file's DPR metadata.
bool drawImageBackground(QPainter& painter, const QRect& rect, QImage image,
                         const QColorSpace& targetSpace, QSize originalSize = {})
{
    if (image.isNull()) return false;
    if (!image.colorSpace().isValid()) image.setColorSpace(QColorSpace::SRgb);
    image.convertToColorSpace(targetSpace.isValid() ? targetSpace : QColorSpace(QColorSpace::SRgb));
    if (image.isNull()) return false;
    if (originalSize.isEmpty()) originalSize = image.size();
    const qreal scale = qMax(qreal(rect.width()) / originalSize.width(), qreal(rect.height()) / originalSize.height());
    // A very thin thumbnail can round to one pixel. Compute the crop against
    // the original aspect and map it into the thumbnail's pixel coordinates.
    const QSizeF crop(rect.width() / scale * image.width() / originalSize.width(),
                      rect.height() / scale * image.height() / originalSize.height());
    const QPointF origin((image.width() - crop.width()) / 2, (image.height() - crop.height()) / 2);
    painter.drawImage(rect, image, QRectF(origin, crop));
    return true;
}

QColorSpace sourceColorSpace(const QPixmap& source) { return source.toImage().colorSpace(); }
QColorSpace sourceColorSpace(const QImage& source) { return source.colorSpace(); }

// The picture inside the frame, clipped to the rounded corners. False, with
// nothing drawn, when an intermediate image cannot be allocated.
template <typename Source>
bool drawScreenshot(QPainter& painter, const QRect& insetRect,
                    const Source& source, const BeautifySettings& settings)
{
    if (settings.cornerRadius <= 0) {
        paintSource(painter, insetRect, source);
        return true;
    }

    // Use CompositionMode_DestinationIn with alpha mask for high-quality anti-aliased corners
    // (matches RegionExportManager::applyRoundedCorners pattern)
    const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
    const QSize deviceSize = CoordinateHelper::toPhysical(insetRect.size(), dpr);

    QImage screenshotImage(deviceSize, QImage::Format_ARGB32_Premultiplied);
    QImage alphaMask(deviceSize, QImage::Format_ARGB32_Premultiplied);
    if (screenshotImage.isNull() || alphaMask.isNull()) {
        return false;
    }
    screenshotImage.setDevicePixelRatio(dpr);
    screenshotImage.fill(Qt::transparent);

    QPainter imgPainter(&screenshotImage);
    imgPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    paintSource(imgPainter, QRect(0, 0, insetRect.width(), insetRect.height()), source);

    alphaMask.setDevicePixelRatio(dpr);
    alphaMask.fill(Qt::transparent);
    QPainter maskPainter(&alphaMask);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath maskPath;
    maskPath.addRoundedRect(QRectF(0, 0, insetRect.width(), insetRect.height()),
                            settings.cornerRadius, settings.cornerRadius);
    maskPainter.fillPath(maskPath, Qt::white);
    maskPainter.end();

    imgPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    imgPainter.drawImage(0, 0, alphaMask);
    imgPainter.end();

    painter.drawImage(insetRect, screenshotImage);
    return true;
}

// Paints the whole beautified picture onto target, which already has the
// output's device size and the source's device pixel ratio. False when the
// picture itself could not be drawn.
template <typename Source>
bool paintBeautified(QPaintDevice& target, const QSize& logicalSource, const QSize& logicalOutput,
                     const Source& source, const BeautifySettings& settings)
{
    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Draw in logical coordinates (QPainter scales by DPR automatically)
    QRect logicalRect(0, 0, logicalOutput.width(), logicalOutput.height());
    if (settings.backgroundType == BeautifyBackgroundType::Image) {
        if (!drawImageBackground(painter, logicalRect,
                SnapTray::BeautifyBackgroundImage::decode(settings.backgroundImageData),
                sourceColorSpace(source))) return false;
    } else {
        BeautifyRenderer::drawBackground(painter, logicalRect, settings);
    }

    QRect insetRect = BeautifyRenderer::calculateInsetRect(logicalOutput, logicalSource, settings);
    BeautifyRenderer::drawShadow(painter, insetRect, settings);
    const bool drawn = drawScreenshot(painter, insetRect, source, settings);
    BeautifyRenderer::drawBorder(painter, insetRect, settings);

    painter.end();
    return drawn;
}

} // namespace

QPixmap BeautifyRenderer::applyToPixmap(const QPixmap& source, const BeautifySettings& settings)
{
    if (source.isNull()) return {};
    if (settings.backgroundType == BeautifyBackgroundType::Image) {
        return QPixmap::fromImage(applyToImage(source.toImage(), settings));
    }

    const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
    const QSize logicalSource = CoordinateHelper::toLogical(source.size(), dpr);
    QSize logicalOutput = calculateOutputSize(logicalSource, settings);
    const QSize deviceOutput = CoordinateHelper::toPhysical(logicalOutput, dpr);

    QPixmap result(deviceOutput);
    if (result.isNull()) return {};
    result.fill(Qt::transparent);
    result.setDevicePixelRatio(dpr);
    if (!paintBeautified(result, logicalSource, logicalOutput, source, settings)) return {};
    return result;
}

QImage BeautifyRenderer::applyToImage(const QImage& source, const BeautifySettings& settings)
{
    if (source.isNull()) return {};

    const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
    const QSize logicalSource = CoordinateHelper::toLogical(source.size(), dpr);
    const QSize logicalOutput = calculateOutputSize(logicalSource, settings);
    const QSize deviceOutput = CoordinateHelper::toPhysical(logicalOutput, dpr);

    QImage result(deviceOutput, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return {};
    result.fill(Qt::transparent);
    result.setDevicePixelRatio(dpr);
    result.setColorSpace(source.colorSpace());
    if (!paintBeautified(result, logicalSource, logicalOutput, source, settings)) return {};
    return result;
}

QSize BeautifyRenderer::calculateOutputSize(const QSize& sourceSize, const BeautifySettings& settings)
{
    QSize baseSize(sourceSize.width() + 2 * settings.padding,
                   sourceSize.height() + 2 * settings.padding);
    return applyAspectRatio(baseSize, settings.aspectRatio);
}

void BeautifyRenderer::render(QPainter& painter, const QRect& targetRect,
                               const QPixmap& source, const BeautifySettings& settings,
                               const QImage& backgroundPreview, QSize originalBackgroundSize)
{
    if (source.isNull()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Use logical pixel dimensions (matches applyToPixmap behavior)
    const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
    const QSize logicalSource = CoordinateHelper::toLogical(source.size(), dpr);

    // Calculate scaled dimensions to fit the target rect while preserving aspect ratio
    QSize outputSize = calculateOutputSize(logicalSource, settings);
    qreal scale = qMin(static_cast<qreal>(targetRect.width()) / outputSize.width(),
                        static_cast<qreal>(targetRect.height()) / outputSize.height());

    QSize scaledSize(static_cast<int>(outputSize.width() * scale),
                     static_cast<int>(outputSize.height() * scale));

    // Center within targetRect
    int offsetX = targetRect.x() + (targetRect.width() - scaledSize.width()) / 2;
    int offsetY = targetRect.y() + (targetRect.height() - scaledSize.height()) / 2;

    painter.translate(offsetX, offsetY);
    painter.scale(scale, scale);

    QRect fullRect(0, 0, outputSize.width(), outputSize.height());
    if (settings.backgroundType == BeautifyBackgroundType::Image) {
        drawImageBackground(painter, fullRect, backgroundPreview, sourceColorSpace(source), originalBackgroundSize);
    } else {
        drawBackground(painter, fullRect, settings);
    }

    QRect insetRect = calculateInsetRect(outputSize, logicalSource, settings);
    painter.setClipRect(fullRect, Qt::IntersectClip);
    drawShadow(painter, insetRect, settings);
    drawScreenshot(painter, insetRect, source, settings);
    drawBorder(painter, insetRect, settings);

    painter.restore();
}

void BeautifyRenderer::drawBackground(QPainter& painter, const QRect& rect,
                                       const BeautifySettings& settings)
{
    painter.save();
    painter.setPen(Qt::NoPen);

    switch (settings.backgroundType) {
    case BeautifyBackgroundType::Transparent:
        break;
    case BeautifyBackgroundType::Solid:
        painter.setBrush(settings.backgroundColor);
        painter.drawRect(rect);
        break;

    case BeautifyBackgroundType::LinearGradient: {
        // Convert angle to start/end points
        double angleRad = settings.gradientAngle * M_PI / 180.0;
        double cx = rect.center().x();
        double cy = rect.center().y();
        double dx = std::cos(angleRad) * rect.width() / 2.0;
        double dy = std::sin(angleRad) * rect.height() / 2.0;

        QLinearGradient gradient(
            QPointF(cx - dx, cy - dy),
            QPointF(cx + dx, cy + dy));
        gradient.setColorAt(0, settings.gradientStartColor);
        gradient.setColorAt(1, settings.gradientEndColor);

        painter.setBrush(gradient);
        painter.drawRect(rect);
        break;
    }

    case BeautifyBackgroundType::RadialGradient: {
        QRadialGradient gradient(rect.center(), qMax(rect.width(), rect.height()) / 2.0);
        gradient.setColorAt(0, settings.gradientStartColor);
        gradient.setColorAt(1, settings.gradientEndColor);

        painter.setBrush(gradient);
        painter.drawRect(rect);
        break;
    }

    default:
        qWarning() << "BeautifyRenderer: Unknown background type"
                    << static_cast<int>(settings.backgroundType);
        painter.setBrush(settings.backgroundColor);
        painter.drawRect(rect);
        break;
    }

    painter.restore();
}

void BeautifyRenderer::drawShadow(QPainter& painter, const QRect& insetRect,
                                   const BeautifySettings& settings)
{
    if (!settings.shadowEnabled) return;

    painter.save();
    painter.setPen(Qt::NoPen);

    const int blur = settings.shadowBlur;
    const int steps = qMin(blur, 20);
    const int baseAlpha = settings.shadowColor.alpha();

    for (int i = steps; i > 0; --i) {
        float ratio = static_cast<float>(i) / steps;
        int expand = static_cast<int>(blur * ratio);
        int alpha = static_cast<int>(baseAlpha * (1.0f - ratio) * 0.15f);

        QColor c = settings.shadowColor;
        c.setAlpha(alpha);
        painter.setBrush(c);

        QRect shadowRect = insetRect.adjusted(
            -expand + settings.shadowOffsetX,
            -expand + settings.shadowOffsetY,
            expand + settings.shadowOffsetX,
            expand + settings.shadowOffsetY);

        qreal cornerExpand = settings.cornerRadius + expand / 2.0;
        painter.drawRoundedRect(shadowRect, cornerExpand, cornerExpand);
    }

    painter.restore();
}

void BeautifyRenderer::drawBorder(QPainter& painter, const QRect& insetRect,
                                   const BeautifySettings& settings)
{
    if (!settings.borderEnabled || settings.borderWidth <= 0) return;
    const double width = settings.borderWidth;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Inside the picture's edge, so the output size does not change.
    const QRectF edge = QRectF(insetRect).adjusted(width / 2.0, width / 2.0, -width / 2.0, -width / 2.0);
    const double radius = qMax(0.0, settings.cornerRadius - width / 2.0);
    painter.setPen(QPen(settings.borderColor, width));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(edge, radius, radius);
    painter.restore();
}

QRect BeautifyRenderer::calculateInsetRect(const QSize& outputSize, const QSize& sourceSize,
                                            const BeautifySettings& settings)
{
    const int anchor = int(settings.alignment);
    const int slackX = outputSize.width() - sourceSize.width();
    const int slackY = outputSize.height() - sourceSize.height();
    // Anchor within the padded canvas. With Auto, the canvas wraps the
    // padded screenshot exactly; a fixed aspect ratio can add extra space.
    const int paddingX = qMin(settings.padding, slackX / 2);
    const int paddingY = qMin(settings.padding, slackY / 2);
    const int x = paddingX + (anchor % 3) * (slackX - 2 * paddingX) / 2;
    const int y = paddingY + (anchor / 3) * (slackY - 2 * paddingY) / 2;
    return QRect(x, y, sourceSize.width(), sourceSize.height());
}

QSize BeautifyRenderer::applyAspectRatio(const QSize& baseSize, BeautifyAspectRatio ratio)
{
    if (ratio == BeautifyAspectRatio::Auto) return baseSize;

    static const std::map<BeautifyAspectRatio, std::pair<int, int>> kRatios = {
        {BeautifyAspectRatio::Square_1_1,  {1, 1}},
        {BeautifyAspectRatio::Wide_16_9,   {16, 9}},
        {BeautifyAspectRatio::Wide_4_3,    {4, 3}},
        {BeautifyAspectRatio::Tall_9_16,   {9, 16}},
        {BeautifyAspectRatio::Twitter_2_1, {2, 1}},
    };

    auto it = kRatios.find(ratio);
    if (it == kRatios.end()) return baseSize;

    auto [rw, rh] = it->second;
    int w = baseSize.width();
    int h = baseSize.height();

    // Expand to fit the ratio while containing the base size
    if (w * rh > h * rw) {
        h = w * rh / rw;
    } else {
        w = h * rw / rh;
    }

    return QSize(w, h);
}
