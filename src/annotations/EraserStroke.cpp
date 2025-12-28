#include "annotations/EraserStroke.h"

#include <QPainter>
#include <QPainterPath>

EraserStroke::EraserStroke(const QVector<QPointF> &points, int width)
    : m_points(points)
    , m_width(width)
{
}

EraserStroke::EraserStroke(int width)
    : m_width(width)
{
}

void EraserStroke::draw(QPainter &painter) const
{
    if (m_points.isEmpty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);

    QPen pen(QColor(0, 0, 0, 255), m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (m_points.size() == 1) {
        qreal radius = m_width / 2.0;
        painter.drawEllipse(m_points.first(), radius, radius);
    } else {
        QPainterPath path;
        path.moveTo(m_points[0]);
        for (int i = 1; i < m_points.size(); ++i) {
            path.lineTo(m_points[i]);
        }
        painter.drawPath(path);
    }

    painter.restore();
}

QRect EraserStroke::boundingRect() const
{
    if (m_points.isEmpty()) return QRect();

    if (m_boundingRectDirty) {
        qreal minX = m_points[0].x();
        qreal maxX = m_points[0].x();
        qreal minY = m_points[0].y();
        qreal maxY = m_points[0].y();

        for (const QPointF &p : m_points) {
            minX = qMin(minX, p.x());
            maxX = qMax(maxX, p.x());
            minY = qMin(minY, p.y());
            maxY = qMax(maxY, p.y());
        }

        int margin = m_width / 2 + 1;
        m_boundingRectCache = QRect(static_cast<int>(minX) - margin, static_cast<int>(minY) - margin,
                                    static_cast<int>(maxX - minX) + 2 * margin,
                                    static_cast<int>(maxY - minY) + 2 * margin);
        m_boundingRectDirty = false;
    }

    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> EraserStroke::clone() const
{
    return std::make_unique<EraserStroke>(m_points, m_width);
}

void EraserStroke::addPoint(const QPointF &point)
{
    m_points.append(point);

    int margin = m_width / 2 + 1;
    QRect pointRect(static_cast<int>(point.x()) - margin, static_cast<int>(point.y()) - margin,
                    margin * 2, margin * 2);

    if (m_boundingRectDirty || m_boundingRectCache.isEmpty()) {
        m_boundingRectCache = pointRect;
        m_boundingRectDirty = false;
    } else {
        m_boundingRectCache = m_boundingRectCache.united(pointRect);
    }
}
