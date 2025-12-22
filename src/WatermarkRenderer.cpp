#include "WatermarkRenderer.h"
#include <QSettings>
#include <QFontMetrics>
#include <QPainterPath>

static const char* SETTINGS_KEY_WATERMARK_ENABLED = "watermarkEnabled";
static const char* SETTINGS_KEY_WATERMARK_TYPE = "watermarkType";
static const char* SETTINGS_KEY_WATERMARK_TEXT = "watermarkText";
static const char* SETTINGS_KEY_WATERMARK_IMAGE_PATH = "watermarkImagePath";
static const char* SETTINGS_KEY_WATERMARK_OPACITY = "watermarkOpacity";
static const char* SETTINGS_KEY_WATERMARK_POSITION = "watermarkPosition";
static const char* SETTINGS_KEY_WATERMARK_IMAGE_SCALE = "watermarkImageScale";

static const int kWatermarkMargin = 12;
static const int kWatermarkPadding = 8;
static const int kWatermarkFontSize = 10;

void WatermarkRenderer::render(QPainter &painter, const QRect &targetRect, const Settings &settings)
{
    if (!settings.enabled) {
        return;
    }

    if (settings.type == Text) {
        renderText(painter, targetRect, settings);
    } else if (settings.type == Image) {
        renderImage(painter, targetRect, settings);
    }
}

void WatermarkRenderer::renderText(QPainter &painter, const QRect &targetRect, const Settings &settings)
{
    if (settings.text.isEmpty()) {
        return;
    }

    painter.save();

    // Set up font
    QFont font = painter.font();
    font.setPointSize(kWatermarkFontSize);
    painter.setFont(font);

    QFontMetrics fm(font);
    QSize textSize = fm.size(0, settings.text);

    // Add padding to text size for background
    QSize bgSize(textSize.width() + kWatermarkPadding * 2, textSize.height() + kWatermarkPadding * 2);

    // Calculate position
    QRect bgRect = calculateWatermarkRect(targetRect, bgSize, settings.position, kWatermarkMargin);

    // Draw semi-transparent background
    int bgAlpha = static_cast<int>(128 * settings.opacity);
    painter.setBrush(QColor(0, 0, 0, bgAlpha));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bgRect, 4, 4);

    // Draw text
    int textAlpha = static_cast<int>(255 * settings.opacity);
    painter.setPen(QColor(255, 255, 255, textAlpha));
    QRect textRect = bgRect.adjusted(kWatermarkPadding, kWatermarkPadding, -kWatermarkPadding, -kWatermarkPadding);
    painter.drawText(textRect, Qt::AlignCenter, settings.text);

    painter.restore();
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
    QRect imageRect = calculateWatermarkRect(targetRect, scaledSize, settings.position, kWatermarkMargin);

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

    // Check if we have content to render
    if (settings.type == Text && settings.text.isEmpty()) {
        return source;
    }
    if (settings.type == Image && settings.imagePath.isEmpty()) {
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
    settings.type = static_cast<Type>(qsettings.value(SETTINGS_KEY_WATERMARK_TYPE, static_cast<int>(Text)).toInt());
    settings.text = qsettings.value(SETTINGS_KEY_WATERMARK_TEXT, QString()).toString();
    settings.imagePath = qsettings.value(SETTINGS_KEY_WATERMARK_IMAGE_PATH, QString()).toString();
    settings.opacity = qsettings.value(SETTINGS_KEY_WATERMARK_OPACITY, 0.5).toDouble();
    settings.position = static_cast<Position>(qsettings.value(SETTINGS_KEY_WATERMARK_POSITION, static_cast<int>(BottomRight)).toInt());
    settings.imageScale = qsettings.value(SETTINGS_KEY_WATERMARK_IMAGE_SCALE, 100).toInt();

    return settings;
}

void WatermarkRenderer::saveSettings(const Settings &settings)
{
    QSettings qsettings("Victor Fu", "SnapTray");

    qsettings.setValue(SETTINGS_KEY_WATERMARK_ENABLED, settings.enabled);
    qsettings.setValue(SETTINGS_KEY_WATERMARK_TYPE, static_cast<int>(settings.type));
    qsettings.setValue(SETTINGS_KEY_WATERMARK_TEXT, settings.text);
    qsettings.setValue(SETTINGS_KEY_WATERMARK_IMAGE_PATH, settings.imagePath);
    qsettings.setValue(SETTINGS_KEY_WATERMARK_OPACITY, settings.opacity);
    qsettings.setValue(SETTINGS_KEY_WATERMARK_POSITION, static_cast<int>(settings.position));
    qsettings.setValue(SETTINGS_KEY_WATERMARK_IMAGE_SCALE, settings.imageScale);
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
