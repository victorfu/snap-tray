#include "WatermarkRenderer.h"
#include <QSettings>
#include <QSizeF>

static const char* SETTINGS_KEY_WATERMARK_ENABLED = "watermarkEnabled";
static const char* SETTINGS_KEY_WATERMARK_IMAGE_PATH = "watermarkImagePath";
static const char* SETTINGS_KEY_WATERMARK_OPACITY = "watermarkOpacity";
static const char* SETTINGS_KEY_WATERMARK_POSITION = "watermarkPosition";
static const char* SETTINGS_KEY_WATERMARK_IMAGE_SCALE = "watermarkImageScale";
static const char* SETTINGS_KEY_WATERMARK_MARGIN = "watermarkMargin";

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

    logicalScaledSize = scaledImage.size() / dpr;
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
    QSize logicalSize = result.size() / dpr;
    QRect targetRect(QPoint(0, 0), logicalSize);

    renderImage(painter, targetRect, settings);

    painter.end();

    return result;
}

WatermarkRenderer::Settings WatermarkRenderer::loadSettings()
{
    QSettings qsettings("Victor Fu", "SnapTray");
    Settings settings;

    settings.enabled = qsettings.value(SETTINGS_KEY_WATERMARK_ENABLED, false).toBool();
    settings.imagePath = qsettings.value(SETTINGS_KEY_WATERMARK_IMAGE_PATH, QString()).toString();
    settings.opacity = qsettings.value(SETTINGS_KEY_WATERMARK_OPACITY, 0.5).toDouble();
    settings.position = static_cast<Position>(qsettings.value(SETTINGS_KEY_WATERMARK_POSITION, static_cast<int>(BottomRight)).toInt());
    settings.imageScale = qsettings.value(SETTINGS_KEY_WATERMARK_IMAGE_SCALE, 100).toInt();
    settings.margin = qsettings.value(SETTINGS_KEY_WATERMARK_MARGIN, 12).toInt();

    return settings;
}

void WatermarkRenderer::saveSettings(const Settings &settings)
{
    QSettings qsettings("Victor Fu", "SnapTray");

    qsettings.setValue(SETTINGS_KEY_WATERMARK_ENABLED, settings.enabled);
    qsettings.setValue(SETTINGS_KEY_WATERMARK_IMAGE_PATH, settings.imagePath);
    qsettings.setValue(SETTINGS_KEY_WATERMARK_OPACITY, settings.opacity);
    qsettings.setValue(SETTINGS_KEY_WATERMARK_POSITION, static_cast<int>(settings.position));
    qsettings.setValue(SETTINGS_KEY_WATERMARK_IMAGE_SCALE, settings.imageScale);
    qsettings.setValue(SETTINGS_KEY_WATERMARK_MARGIN, settings.margin);
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
