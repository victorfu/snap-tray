#ifndef ICONRENDERER_H
#define ICONRENDERER_H

#include <QHash>
#include <QString>
#include <QRect>
#include <QColor>
#include <QPixmap>

class QPainter;
class QSvgRenderer;
class QObject;

/**
 * @brief Centralized SVG icon manager with HiDPI support.
 *
 * Singleton class that handles loading, caching, and rendering of SVG icons
 * with color tinting capability. Used by ScreenCanvas and RegionSelector.
 */
class IconRenderer
{
public:
    /**
     * @brief Get the singleton instance.
     */
    static IconRenderer& instance();

    /**
     * @brief Load an SVG icon from a resource path.
     * @param key Unique identifier for the icon
     * @param resourcePath Qt resource path (e.g., ":/icons/icons/pencil.svg")
     * @return true if loaded successfully
     */
    bool loadIcon(const QString& key, const QString& resourcePath);

    /**
     * @brief Check if an icon is loaded.
     * @param key Icon identifier
     */
    bool hasIcon(const QString& key) const;

    /**
     * @brief Render an icon with color tinting.
     * @param painter The QPainter to render with
     * @param rect Target rectangle for the icon
     * @param key Icon identifier
     * @param tintColor Color to tint the icon
     * @param padding Padding inside the rect (default 8px)
     */
    void renderIcon(QPainter& painter, const QRect& rect,
                    const QString& key, const QColor& tintColor,
                    int padding = 8);

    /**
     * @brief Render an icon preserving its aspect ratio to fit the rect.
     * @param painter The QPainter to render with
     * @param rect Target rectangle for the icon
     * @param key Icon identifier
     * @param tintColor Color to tint the icon
     *
     * Unlike renderIcon(), this method preserves the SVG's original aspect ratio
     * and scales it to fit within the rect (useful for non-square icons like arrows).
     */
    void renderIconFit(QPainter& painter, const QRect& rect,
                       const QString& key, const QColor& tintColor);

    /**
     * @brief Clear all cached icons and free resources.
     */
    void clearCache();

    // Prevent copying
    IconRenderer(const IconRenderer&) = delete;
    IconRenderer& operator=(const IconRenderer&) = delete;

private:
    IconRenderer();
    ~IconRenderer();

    // Pixmap 快取鍵值
    struct PixmapCacheKey {
        QString iconKey;
        int width;
        int height;
        QRgb color;
        int dpr100;  // dpr * 100 轉為整數避免浮點比較問題

        bool operator==(const PixmapCacheKey& other) const {
            return iconKey == other.iconKey && width == other.width
                && height == other.height && color == other.color
                && dpr100 == other.dpr100;
        }
    };

    friend size_t qHash(const PixmapCacheKey& key, size_t seed);

    QHash<QString, QSvgRenderer*> m_renderers;
    QHash<PixmapCacheKey, QPixmap> m_pixmapCache;
};

#endif // ICONRENDERER_H
