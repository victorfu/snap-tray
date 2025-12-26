#ifndef MARKERSTROKE_H
#define MARKERSTROKE_H

#include "AnnotationItem.h"
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QPixmap>

class QPainterPath;

/**
 * @brief Semi-transparent marker/highlighter stroke annotation
 */
class MarkerStroke : public AnnotationItem
{
public:
    MarkerStroke(const QVector<QPointF> &points, const QColor &color, int width);

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

    // Pixmap cache for draw() optimization
    mutable QPixmap m_cachedPixmap;
    mutable QPoint m_cachedOrigin;
    mutable qreal m_cachedDpr = 0.0;
    mutable int m_cachedPointCount = 0;

    // Performance optimization: cached bounding rect
    mutable QRect m_boundingRectCache;
    mutable bool m_boundingRectDirty = true;
};

#endif // MARKERSTROKE_H
