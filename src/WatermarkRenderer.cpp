#include "WatermarkRenderer.h"
#include <QSettings>

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

    painter.save();

    // Scale the image
    qreal scale = settings.imageScale / 100.0;
    QSize scaledSize = watermarkImage.size() * scale;
    QPixmap scaledImage = watermarkImage.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Calculate position
    QRect imageRect = calculateWatermarkRect(targetRect, scaledSize, settings.position, settings.margin);

    // Apply opacity
    painter.setOpacity(settings.opacity);
    painter.drawPixmap(imageRect.topLeft(), scaledImage);

    painter.restore();
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
    painter.setRenderHint(QPainter::Antialiasing);

    render(painter, result.rect(), settings);

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
