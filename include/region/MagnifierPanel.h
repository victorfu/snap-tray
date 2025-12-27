#ifndef MAGNIFIERPANEL_H
#define MAGNIFIERPANEL_H

#include <QObject>
#include <QPixmap>
#include <QImage>
#include <QPoint>
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
              const QSize& viewportSize, const QImage& backgroundImage);

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
     * @brief Check if enough time has passed for an update.
     * @return true if update should proceed, false to skip
     */
    bool shouldUpdate() const;

    /**
     * @brief Reset the update timer after an update.
     */
    void markUpdated();

    // Layout constants
    static constexpr int kWidth = 180;
    static constexpr int kHeight = 120;
    static constexpr int kGridCountX = 15;
    static constexpr int kGridCountY = 10;
    static constexpr int kMinUpdateMs = 16;  // ~60fps cap

private:
    void initializeGridCache();
    void updateMagnifierCache(const QPoint& cursorPos, const QImage& backgroundImage);
    void drawInfoPanel(QPainter& painter, int panelX, int infoY, int panelWidth);
    
    /**
     * @brief Calculate adaptive border color based on background brightness.
     * 
     * Samples pixels around the magnifier area to determine if the background
     * is light or dark, then returns an appropriate contrasting border color.
     * Uses luminance formula (0.299R + 0.587G + 0.114B) for accurate brightness.
     * 
     * @param backgroundImage The background image to sample
     * @param cursorPos The current cursor position
     * @return Dark color for light backgrounds, white for dark backgrounds
     */
    QColor calculateAdaptiveBorderColor(const QImage& backgroundImage, const QPoint& cursorPos) const;

    qreal m_devicePixelRatio = 1.0;
    bool m_showHexColor = false;

    // Cache
    QPixmap m_gridOverlayCache;
    QPixmap m_magnifierPixmapCache;
    QPoint m_cachedDevicePosition;
    bool m_cacheValid = false;

    // Current state
    QColor m_currentColor;
    QPoint m_currentCursorPos;

    // Update throttling
    mutable qint64 m_lastUpdateTime = 0;
};

#endif // MAGNIFIERPANEL_H
