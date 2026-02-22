#ifndef SHAPEANNOTATION_H
#define SHAPEANNOTATION_H

#include "AnnotationItem.h"
#include <QRect>
#include <QRectF>
#include <QPointF>
#include <QColor>
#include <QPolygonF>
#include <QTransform>

// Shape types for ShapeAnnotation
enum class ShapeType {
    Rectangle = 0,
    Ellipse = 1
};

// Fill mode for Shape tool
enum class ShapeFillMode { Outline, Filled };

/**
 * @brief Shape annotation (rectangle or ellipse)
 *
 * Unified annotation class that supports both rectangle and ellipse shapes
 * with optional fill mode.
 */
class ShapeAnnotation : public AnnotationItem
{
public:
    static constexpr qreal kMinScale = 0.1;
    static constexpr qreal kMaxScale = 10.0;
    static constexpr int kHitMargin = 8;

    ShapeAnnotation(const QRect &rect, ShapeType type,
                    const QColor &color, int width, bool filled = false);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;
    void translate(const QPointF& delta) override;

    void setRect(const QRect &rect);
    QRect rect() const { return m_rect.toAlignedRect(); }
    QRectF rectF() const { return m_rect; }
    ShapeType shapeType() const { return m_type; }
    qreal rotation() const { return m_rotation; }
    qreal scaleX() const { return m_scaleX; }
    qreal scaleY() const { return m_scaleY; }

    void setRotation(qreal degrees);
    void setScale(qreal scaleX, qreal scaleY);
    QPointF center() const;
    void moveBy(const QPointF& delta);
    bool containsPoint(const QPoint &point) const;
    QPolygonF transformedBoundingPolygon() const;

private:
    QRectF normalizedRect() const;
    QTransform localLinearTransform() const;
    bool containsTransformedPoint(const QPointF& transformedPoint) const;

    QRectF m_rect;
    ShapeType m_type;
    QColor m_color;
    int m_width;
    bool m_filled;
    qreal m_rotation = 0.0;
    qreal m_scaleX = 1.0;
    qreal m_scaleY = 1.0;
};

#endif // SHAPEANNOTATION_H
