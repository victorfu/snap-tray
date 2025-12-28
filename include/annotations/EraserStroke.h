#ifndef ERASERSTROKE_H
#define ERASERSTROKE_H

#include "AnnotationItem.h"

#include <QPointF>
#include <QRect>
#include <QVector>

/**
 * @brief Eraser stroke that clears annotation pixels.
 */
class EraserStroke : public AnnotationItem
{
public:
    EraserStroke(const QVector<QPointF> &points, int width);
    explicit EraserStroke(int width);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void addPoint(const QPointF &point);
    int pointCount() const { return m_points.size(); }
    int width() const { return m_width; }

private:
    QVector<QPointF> m_points;
    int m_width;

    mutable QRect m_boundingRectCache;
    mutable bool m_boundingRectDirty = true;
};

#endif // ERASERSTROKE_H
