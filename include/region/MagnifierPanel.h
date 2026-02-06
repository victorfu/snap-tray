#ifndef MAGNIFIERPANEL_H
#define MAGNIFIERPANEL_H

#include <QObject>
#include <QPixmap>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QColor>

class QPainter;

/**
 * @brief MagnifierPanel handles magnifier rendering and caching for region selection.
 *
 * Provides a zoomed-in view of the area around the cursor, with:
 * - Grid overlay showing individual pixels
 * - Coordinate display
 * - Color value display (RGB or HEX format)
 * - Hotkey instructions
 */
class MagnifierPanel : public QObject
{
    Q_OBJECT

public:
    explicit MagnifierPanel(QObject* parent = nullptr);
    ~MagnifierPanel() override = default;

    // Configuration
    void setDevicePixelRatio(qreal ratio) { m_devicePixelRatio = ratio; }
    void setShowHexColor(bool show) { m_showHexColor = show; }
    bool showHexColor() const { return m_showHexColor; }
    void toggleColorFormat() { m_showHexColor = !m_showHexColor; }

    /**
     * @brief Draw the magnifier panel at the specified cursor position.
     * @param painter The painter to draw with
     * @param cursorPos The current cursor position (logical coordinates)
     * @param viewportSize The size of the viewport (for boundary checking)
     * @param backgroundImage The background image to magnify (device pixels)
     */
    void draw(QPainter& painter, const QPoint& cursorPos,
              const QSize& viewportSize, const QPixmap& backgroundPixmap);

    /**
     * @brief Calculate the panel rect for the current cursor position.
     * @param cursorPos The current cursor position (logical coordinates)
     * @param viewportSize The size of the viewport (for boundary checking)
     * @return Panel rect including magnifier and info panel.
     */
    static QRect panelRectForCursor(const QPoint& cursorPos, const QSize& viewportSize);

    /**
     * @brief Get the color at the current cursor position.
     * @return The color at the cursor, or black if out of bounds.
     */
    QColor currentColor() const { return m_currentColor; }

    /**
     * @brief Get the formatted color string (RGB or HEX).
     */
    QString colorString() const;

    /**
     * @brief Invalidate the magnifier cache (call when background changes).
     */
    void invalidateCache();

    /**
     * @brief Pre-warm the magnifier cache for the initial cursor position.
     * Call this after initialization to eliminate first-frame delay.
     * @param cursorPos The initial cursor position (logical coordinates)
     * @param backgroundPixmap The background pixmap to magnify
     */
    void preWarmCache(const QPoint& cursorPos, const QPixmap& backgroundPixmap);

    // Layout constants
    static constexpr int kWidth = 180;
    static constexpr int kHeight = 120;
    static constexpr int kGridCountX = 15;
    static constexpr int kGridCountY = 10;
    static constexpr int kMinUpdateMs = 16;  // ~60fps cap

private:
    void initializeGridCache();
    void updateMagnifierCache(const QPoint& cursorPos, const QPixmap& backgroundPixmap);
    void drawInfoPanel(QPainter& painter, int panelX, int infoY, int panelWidth);

    qreal m_devicePixelRatio = 1.0;
    bool m_showHexColor = false;

    // Cache
    QPixmap m_gridOverlayCache;
    QPixmap m_magnifierPixmapCache;
    QPoint m_cachedDevicePosition;
    bool m_cacheValid = false;
    QImage m_backgroundImageCache;  // Cached QImage to avoid toImage() every frame

    // Current state
    QColor m_currentColor;
    QPoint m_currentCursorPos;
};

#endif // MAGNIFIERPANEL_H
