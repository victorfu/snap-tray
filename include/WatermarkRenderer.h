#ifndef WATERMARKRENDERER_H
#define WATERMARKRENDERER_H

#include "settings/WatermarkSettingsManager.h"

#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QRect>

class WatermarkRenderer
{
public:
    using Position = WatermarkSettingsManager::Position;
    using Settings = WatermarkSettingsManager::Settings;

    // Position enum values for backwards compatibility
    static constexpr Position TopLeft = WatermarkSettingsManager::TopLeft;
    static constexpr Position TopRight = WatermarkSettingsManager::TopRight;
    static constexpr Position BottomLeft = WatermarkSettingsManager::BottomLeft;
    static constexpr Position BottomRight = WatermarkSettingsManager::BottomRight;

    // Render watermark directly onto a painter (for display)
    static void render(QPainter &painter, const QRect &targetRect, const Settings &settings);

    // Apply watermark to a pixmap and return a new pixmap (for save/copy)
    static QPixmap applyToPixmap(const QPixmap &source, const Settings &settings);

    // Apply using a pre-cached QImage (thread-safe, for background encoding)
    static QImage applyToImageWithCache(const QImage &source,
                                        const QImage &cachedWatermark,
                                        const Settings &settings);

private:
    static QRect calculateWatermarkRect(const QRect &targetRect, const QSize &size, Position position, int margin);
    static void renderImage(QPainter &painter, const QRect &targetRect, const Settings &settings);
};

#endif // WATERMARKRENDERER_H
