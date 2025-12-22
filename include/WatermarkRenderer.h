#ifndef WATERMARKRENDERER_H
#define WATERMARKRENDERER_H

#include <QString>
#include <QPixmap>
#include <QPainter>
#include <QRect>

class WatermarkRenderer
{
public:
    enum Position {
        TopLeft = 0,
        TopRight = 1,
        BottomLeft = 2,
        BottomRight = 3
    };

    enum Type {
        Text = 0,
        Image = 1
    };

    struct Settings {
        bool enabled = false;
        Type type = Text;
        QString text;
        QString imagePath;
        qreal opacity = 0.5;
        Position position = BottomRight;
        int imageScale = 100;  // 10-200%
    };

    // Render watermark directly onto a painter (for display)
    static void render(QPainter &painter, const QRect &targetRect, const Settings &settings);

    // Apply watermark to a pixmap and return a new pixmap (for save/copy)
    static QPixmap applyToPixmap(const QPixmap &source, const Settings &settings);

    // Load settings from QSettings
    static Settings loadSettings();

    // Save settings to QSettings
    static void saveSettings(const Settings &settings);

private:
    static QRect calculateWatermarkRect(const QRect &targetRect, const QSize &size, Position position, int margin);
    static void renderText(QPainter &painter, const QRect &targetRect, const Settings &settings);
    static void renderImage(QPainter &painter, const QRect &targetRect, const Settings &settings);
};

#endif // WATERMARKRENDERER_H
