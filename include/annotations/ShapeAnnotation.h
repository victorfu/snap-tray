#ifndef SHAPEANNOTATION_H
#define SHAPEANNOTATION_H

#include "AnnotationItem.h"
#include <QRect>
#include <QColor>

// Shape types for ShapeAnnotation
enum class ShapeType {
    Rectangle = 0,
    Ellipse = 1
};

/**
 * @brief Shape annotation (rectangle or ellipse)
 *
 * Unified annotation class that supports both rectangle and ellipse shapes
 * with optional fill mode.
 */
class ShapeAnnotation : public AnnotationItem
{
public:
    ShapeAnnotation(const QRect &rect, ShapeType type,
                    const QColor &color, int width, bool filled = false);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void setRect(const QRect &rect);
    QRect rect() const { return m_rect; }
    ShapeType shapeType() const { return m_type; }

private:
    QRect m_rect;
    ShapeType m_type;
    QColor m_color;
    int m_width;
    bool m_filled;
};

#endif // SHAPEANNOTATION_H
