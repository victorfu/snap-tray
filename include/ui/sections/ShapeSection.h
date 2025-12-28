#ifndef SHAPESECTION_H
#define SHAPESECTION_H

#include <QObject>
#include <QRect>
#include <QVector>
#include "ui/IWidgetSection.h"
#include "annotations/ShapeAnnotation.h"  // For ShapeType and ShapeFillMode enums

/**
 * @brief Shape type and fill mode section for ColorAndWidthWidget.
 *
 * Provides buttons to select shape type (Rectangle/Ellipse) and toggle fill mode.
 */
class ShapeSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit ShapeSection(QObject* parent = nullptr);

    // =========================================================================
    // Shape Management
    // =========================================================================

    void setShapeType(ShapeType type);
    ShapeType shapeType() const { return m_shapeType; }

    void setShapeFillMode(ShapeFillMode mode);
    ShapeFillMode shapeFillMode() const { return m_shapeFillMode; }

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
    void shapeTypeChanged(ShapeType type);
    void shapeFillModeChanged(ShapeFillMode mode);

private:
    /**
     * @brief Get the button at the given position.
     * @return Button index: 0=Rectangle, 1=Ellipse, 2=FillMode, -1=none
     */
    int buttonAtPosition(const QPoint& pos) const;

    // State
    ShapeType m_shapeType = ShapeType::Rectangle;
    ShapeFillMode m_shapeFillMode = ShapeFillMode::Outline;
    int m_hoveredButton = -1;

    // Layout
    QRect m_sectionRect;
    QVector<QRect> m_buttonRects;  // 3 buttons: [Rect, Ellipse, FillToggle]

    // Layout constants
    static constexpr int BUTTON_SIZE = 22;  // Reduced from 24 for 32px widget height
    static constexpr int BUTTON_SPACING = 2;
    static constexpr int SECTION_PADDING = 6;
};

#endif // SHAPESECTION_H
