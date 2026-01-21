#include "WatermarkRenderer.h"
#include "utils/CoordinateHelper.h"

#include <QSizeF>

void WatermarkRenderer::render(QPainter &painter, const QRect &targetRect, const Settings &settings)
{
    if (!settings.enabled) {
        return;
    }

    renderImage(painter, targetRect, settings);
}

void WatermarkRenderer::renderImage(QPainter &painter, const QRect &targetRect, const Settings &settings)
{
    if (settings.imagePath.isEmpty()) {
        return;
    }

    QPixmap watermarkImage(settings.imagePath);
    if (watermarkImage.isNull()) {
        return;
    }

    qreal dpr = 1.0;
    if (painter.device()) {
        dpr = painter.device()->devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
    }

    qreal watermarkDpr = watermarkImage.devicePixelRatio();
    if (watermarkDpr <= 0.0) {
        watermarkDpr = 1.0;
    }
    QSizeF watermarkLogicalSize = QSizeF(watermarkImage.size()) / watermarkDpr;
    qreal scale = settings.imageScale / 100.0;
    QSizeF logicalScaledSizeF = watermarkLogicalSize * scale;
    QSize logicalScaledSize = logicalScaledSizeF.toSize();
    if (logicalScaledSize.isEmpty()) {
        return;
    }

    QSize deviceScaledSize = (logicalScaledSizeF * dpr).toSize();
    if (deviceScaledSize.isEmpty()) {
        return;
    }

    QPixmap scaledImage = watermarkImage.scaled(deviceScaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaledImage.setDevicePixelRatio(dpr);

    if (scaledImage.isNull()) {
        return;
    }

    logicalScaledSize = CoordinateHelper::toLogical(scaledImage.size(), dpr);
    if (logicalScaledSize.isEmpty()) {
        return;
    }

    int margin = settings.margin;
    QRect imageRect = calculateWatermarkRect(targetRect, logicalScaledSize, settings.position, margin);

    // Apply opacity and draw watermark
    painter.setOpacity(settings.opacity);
    painter.drawPixmap(imageRect.topLeft(), scaledImage);
}

QPixmap WatermarkRenderer::applyToPixmap(const QPixmap &source, const Settings &settings)
{
    if (!settings.enabled) {
        return source;
    }

    if (settings.imagePath.isEmpty()) {
        return source;
    }

    QPixmap result = source.copy();
    QPainter painter(&result);

    if (!painter.isActive()) {
        return source;
    }

    painter.setRenderHint(QPainter::Antialiasing);

    qreal dpr = result.devicePixelRatio();
    if (dpr <= 0.0) {
        dpr = 1.0;
    }
    QSize logicalSize = CoordinateHelper::toLogical(result.size(), dpr);
    QRect targetRect(QPoint(0, 0), logicalSize);

    renderImage(painter, targetRect, settings);

    painter.end();

    return result;
}

void WatermarkRenderer::renderWithCache(QPainter &painter,
                                        const QRect &targetRect,
                                        const QPixmap &cachedWatermark,
                                        const Settings &settings)
{
    if (!settings.enabled || cachedWatermark.isNull()) {
        return;
    }

    qreal dpr = 1.0;
    if (painter.device()) {
        dpr = painter.device()->devicePixelRatio();
        if (dpr <= 0.0) {
            dpr = 1.0;
        }
    }

    // The cached watermark is already scaled, just need to handle DPR
    qreal watermarkDpr = cachedWatermark.devicePixelRatio();
    if (watermarkDpr <= 0.0) {
        watermarkDpr = 1.0;
    }

    QPixmap scaledImage = cachedWatermark;
    if (qFuzzyCompare(dpr, watermarkDpr)) {
        // DPR matches, use as-is
        scaledImage.setDevicePixelRatio(dpr);
    } else {
        // Need to adjust for target DPR
        QSize deviceSize = (QSizeF(cachedWatermark.size()) / watermarkDpr * dpr).toSize();
        if (!deviceSize.isEmpty()) {
            scaledImage = cachedWatermark.scaled(deviceSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            scaledImage.setDevicePixelRatio(dpr);
        }
    }

    QSize logicalSize = CoordinateHelper::toLogical(scaledImage.size(), dpr);
    if (logicalSize.isEmpty()) {
        return;
    }

    QRect imageRect = calculateWatermarkRect(targetRect, logicalSize, settings.position, settings.margin);

    painter.setOpacity(settings.opacity);
    painter.drawPixmap(imageRect.topLeft(), scaledImage);
}

QPixmap WatermarkRenderer::applyToPixmapWithCache(const QPixmap &source,
                                                  const QPixmap &cachedWatermark,
                                                  const Settings &settings)
{
    if (!settings.enabled || cachedWatermark.isNull()) {
        return source;
    }

    QPixmap result = source.copy();
    QPainter painter(&result);

    if (!painter.isActive()) {
        return source;
    }

    painter.setRenderHint(QPainter::Antialiasing);

    qreal dpr = result.devicePixelRatio();
    if (dpr <= 0.0) {
        dpr = 1.0;
    }
    QSize logicalSize = CoordinateHelper::toLogical(result.size(), dpr);
    QRect targetRect(QPoint(0, 0), logicalSize);

    renderWithCache(painter, targetRect, cachedWatermark, settings);

    painter.end();

    return result;
}

QImage WatermarkRenderer::applyToImageWithCache(const QImage &source,
                                                  const QImage &cachedWatermark,
                                                  const Settings &settings)
{
    if (!settings.enabled || cachedWatermark.isNull()) {
        return source;
    }

    QImage result = source.copy();
    QPainter painter(&result);

    if (!painter.isActive()) {
        return source;
    }

    painter.setRenderHint(QPainter::Antialiasing);

    QRect targetRect(QPoint(0, 0), result.size());
    QRect imageRect = calculateWatermarkRect(targetRect, cachedWatermark.size(), settings.position, settings.margin);

    painter.setOpacity(settings.opacity);
    painter.drawImage(imageRect.topLeft(), cachedWatermark);
    painter.end();

    return result;
}

QRect WatermarkRenderer::calculateWatermarkRect(const QRect &targetRect, const QSize &size, Position position, int margin)
{
    int x, y;

    switch (position) {
    case TopLeft:
        x = targetRect.left() + margin;
        y = targetRect.top() + margin;
        break;
    case TopRight:
        x = targetRect.right() - size.width() - margin;
        y = targetRect.top() + margin;
        break;
    case BottomLeft:
        x = targetRect.left() + margin;
        y = targetRect.bottom() - size.height() - margin;
        break;
    case BottomRight:
    default:
        x = targetRect.right() - size.width() - margin;
        y = targetRect.bottom() - size.height() - margin;
        break;
    }

    return QRect(x, y, size.width(), size.height());
}
