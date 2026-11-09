#pragma once

#include "annotations/ArrowAnnotation.h"
#include <QPainterPath>
#include <QVector>

// Shared painted geometry for arrows and polylines. Building it from current
// points keeps drawing, bounds and hit testing in sync after every mutation.
struct LineAnnotationGeometry
{
    struct Head {
        QPainterPath path;
        bool filled;
    };

    QPainterPath shaft;
    QVector<Head> heads;
    Qt::PenCapStyle capStyle = Qt::FlatCap;

    static qreal headLength(int width);
    static qreal headBaseDistance(int width);
    void addHead(const QPointF& tip, qreal angle, int width, LineEndStyle style,
                 qreal length = -1.0);
    void addHeadFromBase(const QPointF& tip, const QPointF& base, int width, LineEndStyle style);
    void draw(QPainter& painter, const QColor& color, int width, LineStyle style) const;
    QRect boundingRect(int width) const;
    bool containsPoint(const QPoint& point, int width) const;
};
