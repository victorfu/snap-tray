#include "annotations/PolylineAnnotation.h"
#include "annotations/LineAnnotationGeometry.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

PolylineAnnotation::PolylineAnnotation(const QColor& color, int width,
                                       LineEndStyle style, LineStyle lineStyle)
    : m_color(color)
    , m_width(width)
    , m_lineEndStyle(style)
    , m_lineStyle(lineStyle)
{
}

PolylineAnnotation::PolylineAnnotation(const QVector<QPoint>& points, const QColor& color,
                                       int width, LineEndStyle style, LineStyle lineStyle)
    : m_points(points)
    , m_color(color)
    , m_width(width)
    , m_lineEndStyle(style)
    , m_lineStyle(lineStyle)
{
}

void PolylineAnnotation::draw(QPainter& painter) const
{
    geometry().draw(painter, m_color, m_width, m_lineStyle);
}

LineAnnotationGeometry PolylineAnnotation::geometry() const
{
    LineAnnotationGeometry geometry;
    if (m_points.size() < 2) {
        return geometry;
    }
    // Check if we have arrowheads
    bool hasEndArrow = (m_lineEndStyle != LineEndStyle::None);
    bool hasStartArrow = (m_lineEndStyle == LineEndStyle::BothArrow ||
                          m_lineEndStyle == LineEndStyle::BothArrowOutline);
    bool needsEndAdjust = (m_lineEndStyle == LineEndStyle::EndArrow ||
                           m_lineEndStyle == LineEndStyle::EndArrowOutline ||
                           m_lineEndStyle == LineEndStyle::BothArrow ||
                           m_lineEndStyle == LineEndStyle::BothArrowOutline);
    bool needsStartAdjust = hasStartArrow;

    geometry.capStyle = hasEndArrow ? Qt::FlatCap : Qt::RoundCap;
    const qreal arrowLength = LineAnnotationGeometry::headLength(m_width);
    const qreal baseDistance = LineAnnotationGeometry::headBaseDistance(m_width);
    int lastIdx = m_points.size() - 1;

    // Determine where to draw the end arrow
    // To avoid jitter at inflection points, keep arrow at previous vertex
    // until the new segment is long enough.
    int arrowTipIdx = lastIdx;
    if (m_points.size() > 2) {
        double lastSegmentLen = QLineF(m_points[lastIdx-1], m_points[lastIdx]).length();
        if (lastSegmentLen < arrowLength) {
            arrowTipIdx = lastIdx - 1;
        }
    }

    // Build the path for all segments

    // Adjust first point for start arrowhead
    QPointF startPoint = m_points[0];
    if (needsStartAdjust && m_points.size() >= 2) {
        double angle = qAtan2(m_points[0].y() - m_points[1].y(),
                             m_points[0].x() - m_points[1].x());
        startPoint = QPointF(
            m_points[0].x() - baseDistance * qCos(angle),
            m_points[0].y() - baseDistance * qSin(angle)
        );
    }
    geometry.shaft.moveTo(startPoint);

    // Draw segments
    for (int i = 1; i <= lastIdx; ++i) {
        QPointF target = m_points[i];

        // Check if this vertex needs adjustment for the end arrow
        if (needsEndAdjust && i == arrowTipIdx) {
             const QPoint& prev = m_points[i - 1];
             double angle = qAtan2(target.y() - prev.y(), target.x() - prev.x());
             target = QPointF(
                target.x() - baseDistance * qCos(angle),
                target.y() - baseDistance * qSin(angle)
             );
        }

        geometry.shaft.lineTo(target);
    }

    const QPointF endDirection = m_points[arrowTipIdx] - m_points[arrowTipIdx - 1];
    geometry.addHead(m_points[arrowTipIdx], qAtan2(endDirection.y(), endDirection.x()),
                     m_width, m_lineEndStyle);
    if (hasStartArrow) {
        const QPointF startDirection = m_points[0] - m_points[1];
        geometry.addHead(m_points[0], qAtan2(startDirection.y(), startDirection.x()),
                         m_width, m_lineEndStyle);
    }
    return geometry;
}

QRect PolylineAnnotation::boundingRect() const
{
    if (m_points.size() == 1) {
        // Keep the initial vertex available to placement/selection callers.
        return QRect(m_points.first(), QSize(1, 1));
    }
    return geometry().boundingRect(m_width);
}

std::unique_ptr<AnnotationItem> PolylineAnnotation::clone() const
{
    return std::make_unique<PolylineAnnotation>(m_points, m_color, m_width, m_lineEndStyle, m_lineStyle);
}

void PolylineAnnotation::addPoint(const QPoint& point)
{
    m_points.append(point);
}

void PolylineAnnotation::updateLastPoint(const QPoint& point)
{
    if (!m_points.isEmpty()) {
        m_points.last() = point;
    }
}

void PolylineAnnotation::setPoint(int index, const QPoint& point)
{
    if (index >= 0 && index < m_points.size()) {
        m_points[index] = point;
    }
}

void PolylineAnnotation::removeLastPoint()
{
    if (!m_points.isEmpty()) {
        m_points.removeLast();
    }
}

bool PolylineAnnotation::containsPoint(const QPoint& pos) const
{
    return geometry().containsPoint(pos, m_width);
}

void PolylineAnnotation::moveBy(const QPoint& delta)
{
    for (QPoint& point : m_points) {
        point += delta;
    }
}

void PolylineAnnotation::translate(const QPointF& delta)
{
    moveBy(delta.toPoint());
}
