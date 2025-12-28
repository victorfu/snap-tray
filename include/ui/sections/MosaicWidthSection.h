#ifndef MOSAICWIDTHSECTION_H
#define MOSAICWIDTHSECTION_H

#include <QObject>
#include <QRect>
#include "ui/IWidgetSection.h"

/**
 * @brief Dedicated width slider section for Mosaic tool.
 *
 * Provides a horizontal slider for adjusting mosaic block size.
 * This section is independent from the shared WidthSection used by other tools.
 */
class MosaicWidthSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit MosaicWidthSection(QObject* parent = nullptr);

    // =========================================================================
    // Width Management
    // =========================================================================

    void setWidthRange(int min, int max);
    void setCurrentWidth(int width);
    int currentWidth() const { return m_currentWidth; }
    int minWidth() const { return m_minWidth; }
    int maxWidth() const { return m_maxWidth; }

    // =========================================================================
    // Wheel Event
    // =========================================================================

    bool handleWheel(int delta);

    // =========================================================================
    // IWidgetSection Implementation
    // =========================================================================

    void updateLayout(int containerTop, int containerHeight, int xOffset) override;
    int preferredWidth() const override;
    QRect boundingRect() const override { return m_sectionRect; }
    void draw(QPainter& painter, const ToolbarStyleConfig& styleConfig) override;
    bool contains(const QPoint& pos) const override;
    bool handleClick(const QPoint& pos) override;
    bool updateHovered(const QPoint& pos) override;
    void resetHoverState() override;

signals:
    void widthChanged(int width);

private:
    // Calculate thumb position from current width
    int thumbPositionFromWidth() const;
    // Calculate width from click position
    int widthFromPosition(int x) const;

    // State
    int m_minWidth = 10;
    int m_maxWidth = 100;
    int m_currentWidth = 30;
    bool m_hovered = false;
    bool m_dragging = false;

    // Layout
    QRect m_sectionRect;
    QRect m_trackRect;
    QRect m_thumbRect;

    // Layout constants
    static constexpr int SECTION_WIDTH = 70;
    static constexpr int TRACK_HEIGHT = 4;
    static constexpr int THUMB_SIZE = 12;
    static constexpr int SECTION_PADDING = 6;
};

#endif // MOSAICWIDTHSECTION_H
