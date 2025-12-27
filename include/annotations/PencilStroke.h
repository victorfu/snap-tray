#ifndef PENCILSTROKE_H
#define PENCILSTROKE_H

#include "AnnotationItem.h"
#include "LineStyle.h"
#include <QVector>
#include <QPointF>
#include <QColor>

class QPainterPath;

/**
 * @brief Freehand pencil stroke annotation
 */
class PencilStroke : public AnnotationItem
{
public:
    PencilStroke(const QVector<QPointF> &points, const QColor &color, int width,
                 LineStyle lineStyle = LineStyle::Solid);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void addPoint(const QPointF &point);

    // Collision detection for eraser (path-based intersection)
    bool intersectsCircle(const QPoint &center, int radius) const;
    QPainterPath strokePath() const;

private:
    QVector<QPointF> m_points;
    QColor m_color;
    int m_width;
    LineStyle m_lineStyle;

    // Performance optimization: cached bounding rect
    mutable QRect m_boundingRectCache;
    mutable bool m_boundingRectDirty = true;
};

#endif // PENCILSTROKE_H
