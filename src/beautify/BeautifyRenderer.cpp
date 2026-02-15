#include "beautify/BeautifyRenderer.h"
#include "utils/CoordinateHelper.h"
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QDebug>
#include <cmath>
#include <map>

QPixmap BeautifyRenderer::applyToPixmap(const QPixmap& source, const BeautifySettings& settings)
{
    if (source.isNull()) return {};

    const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
    const QSize logicalSource = CoordinateHelper::toLogical(source.size(), dpr);
    QSize logicalOutput = calculateOutputSize(logicalSource, settings);
    const QSize deviceOutput = CoordinateHelper::toPhysical(logicalOutput, dpr);

    QPixmap result(deviceOutput);
    result.fill(Qt::transparent);
    result.setDevicePixelRatio(dpr);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Draw in logical coordinates (QPainter scales by DPR automatically)
    QRect logicalRect(0, 0, logicalOutput.width(), logicalOutput.height());
    drawBackground(painter, logicalRect, settings);

    QRect insetRect = calculateInsetRect(logicalOutput, logicalSource, settings);
    drawShadow(painter, insetRect, settings);
    drawScreenshot(painter, insetRect, source, settings);

    painter.end();
    return result;
}

QSize BeautifyRenderer::calculateOutputSize(const QSize& sourceSize, const BeautifySettings& settings)
{
    QSize baseSize(sourceSize.width() + 2 * settings.padding,
                   sourceSize.height() + 2 * settings.padding);
    return applyAspectRatio(baseSize, settings.aspectRatio);
}

void BeautifyRenderer::render(QPainter& painter, const QRect& targetRect,
                               const QPixmap& source, const BeautifySettings& settings)
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
    drawBackground(painter, fullRect, settings);

    QRect insetRect = calculateInsetRect(outputSize, logicalSource, settings);
    drawShadow(painter, insetRect, settings);
    drawScreenshot(painter, insetRect, source, settings);

    painter.restore();
}

void BeautifyRenderer::drawBackground(QPainter& painter, const QRect& rect,
                                       const BeautifySettings& settings)
{
    painter.save();
    painter.setPen(Qt::NoPen);

    switch (settings.backgroundType) {
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

void BeautifyRenderer::drawScreenshot(QPainter& painter, const QRect& insetRect,
                                       const QPixmap& source, const BeautifySettings& settings)
{
    if (settings.cornerRadius <= 0) {
        painter.drawPixmap(insetRect, source);
        return;
    }

    // Use CompositionMode_DestinationIn with alpha mask for high-quality anti-aliased corners
    // (matches RegionExportManager::applyRoundedCorners pattern)
    const qreal dpr = source.devicePixelRatio() > 0.0 ? source.devicePixelRatio() : 1.0;
    const QSize deviceSize = CoordinateHelper::toPhysical(insetRect.size(), dpr);

    QImage screenshotImage(deviceSize, QImage::Format_ARGB32_Premultiplied);
    screenshotImage.setDevicePixelRatio(dpr);
    screenshotImage.fill(Qt::transparent);

    QPainter imgPainter(&screenshotImage);
    imgPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    imgPainter.drawPixmap(QRect(0, 0, insetRect.width(), insetRect.height()), source);

    QImage alphaMask(deviceSize, QImage::Format_ARGB32_Premultiplied);
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
}

QRect BeautifyRenderer::calculateInsetRect(const QSize& outputSize, const QSize& sourceSize,
                                            const BeautifySettings& settings)
{
    Q_UNUSED(settings)
    // Center the source within the output
    int x = (outputSize.width() - sourceSize.width()) / 2;
    int y = (outputSize.height() - sourceSize.height()) / 2;
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
