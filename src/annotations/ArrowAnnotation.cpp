#include "annotations/ArrowAnnotation.h"
#include "annotations/LineAnnotationGeometry.h"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QtMath>
#include <QDebug>

ArrowAnnotation::ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width,
                                 LineEndStyle style, LineStyle lineStyle)
    : m_start(start)
    , m_end(end)
    , m_controlPoint((QPointF(start) + QPointF(end)) / 2.0)  // Default: midpoint = straight line
    , m_color(color)
    , m_width(width)
    , m_lineEndStyle(style)
    , m_lineStyle(lineStyle)
{
}

double ArrowAnnotation::endTangentAngle() const
{
    // Tangent at t=1 for Quadratic Bézier is proportional to (P2 - P1) = (end - control)
    QPointF tangent = m_end - m_controlPoint;

    // Edge case: if end == control, fall back to end - start
    if (tangent.isNull()) {
        tangent = m_end - m_start;
    }

    return qAtan2(tangent.y(), tangent.x());
}

double ArrowAnnotation::startTangentAngle() const
{
    // Tangent at t=0 for Quadratic Bézier is proportional to (P1 - P0) = (control - start)
    QPointF tangent = m_controlPoint - m_start;

    // Edge case: if control == start, fall back to end - start
    if (tangent.isNull()) {
        tangent = m_end - m_start;
    }

    return qAtan2(tangent.y(), tangent.x());
}

void ArrowAnnotation::draw(QPainter &painter) const
{
    geometry().draw(painter, m_color, m_width, m_lineStyle);
}

LineAnnotationGeometry ArrowAnnotation::geometry() const
{
    LineAnnotationGeometry geometry;
    const qreal baseDistance = LineAnnotationGeometry::headBaseDistance(m_width);
    // Check if we have arrowheads
    bool hasEndArrow = (m_lineEndStyle != LineEndStyle::None);
    bool hasStartArrow = (m_lineEndStyle == LineEndStyle::BothArrow ||
                          m_lineEndStyle == LineEndStyle::BothArrowOutline);

    // Calculate adjusted endpoints for the curve (so line doesn't protrude through arrowheads)
    QPointF curveStart = m_start;
    QPointF curveEnd = m_end;

    if (hasEndArrow && (m_lineEndStyle == LineEndStyle::EndArrow ||
                        m_lineEndStyle == LineEndStyle::EndArrowOutline ||
                        m_lineEndStyle == LineEndStyle::BothArrow ||
                        m_lineEndStyle == LineEndStyle::BothArrowOutline)) {
        double angle = endTangentAngle();
        curveEnd = QPointF(
            m_end.x() - baseDistance * qCos(angle),
            m_end.y() - baseDistance * qSin(angle)
        );
    }

    if (hasStartArrow) {
        double angle = startTangentAngle();
        // For start arrowhead, we move in the direction of the tangent (away from start)
        curveStart = QPointF(
            m_start.x() + baseDistance * qCos(angle),
            m_start.y() + baseDistance * qSin(angle)
        );
    }

    // Calculate adjusted control point to maintain curve shape with shortened endpoints
    // We need to find the approximate control point for the shortened curve
    QPointF adjustedControl = m_controlPoint;

    // If we shortened the curve significantly, adjust control point proportionally
    // This keeps the curve shape consistent
    if (hasEndArrow || hasStartArrow) {
        // Simple linear interpolation to keep control point in proper relation
        // For a more accurate solution, we'd need to subdivide the Bézier
        // But this approximation works well for typical arrow lengths
        QPointF originalMid = (QPointF(m_start) + QPointF(m_end)) / 2.0;
        QPointF newMid = (curveStart + curveEnd) / 2.0;
        QPointF offset = QPointF(m_controlPoint) - originalMid;
        adjustedControl = newMid + offset;
    }

    // Draw the Bézier curve using QPainterPath
    // Set NoBrush to prevent filling the curve interior
    geometry.shaft.moveTo(curveStart);
    geometry.shaft.quadTo(adjustedControl, curveEnd);

    geometry.addHead(m_end, endTangentAngle(), m_width, m_lineEndStyle);
    if (hasStartArrow) {
        geometry.addHead(m_start, startTangentAngle() + M_PI, m_width, m_lineEndStyle);
    }
    return geometry;
}

QRect ArrowAnnotation::boundingRect() const
{
    return geometry().boundingRect(m_width);
}

bool ArrowAnnotation::containsPoint(const QPoint &pos) const
{
    return geometry().containsPoint(pos, m_width);
}

bool ArrowAnnotation::isCurved() const
{
    // Check if control point deviates from the line segment between start and end
    // Using cross product to calculate perpendicular distance from point to line
    QPointF lineVec = m_end - m_start;
    QPointF pointVec = m_controlPoint - m_start;

    double cross = lineVec.x() * pointVec.y() - lineVec.y() * pointVec.x();
    double lineLength = qSqrt(lineVec.x() * lineVec.x() + lineVec.y() * lineVec.y());

    if (lineLength < 1.0) {
        return false;  // Degenerate line
    }

    double distance = qAbs(cross) / lineLength;
    return distance > 2.0;  // Tolerance of 2 pixels
}

void ArrowAnnotation::setStart(const QPoint &start)
{
    // When moving start, also move control point to maintain curve shape
    QPoint delta = start - m_start;
    m_start = start;

    // Move control point by half the delta to keep relative curvature
    // This maintains the curve shape when adjusting endpoints
    m_controlPoint += QPointF(delta) / 2.0;
}

void ArrowAnnotation::setEnd(const QPoint &end)
{
    // When moving end, also move control point to maintain curve shape
    QPoint delta = end - m_end;
    m_end = end;

    // Move control point by half the delta to keep relative curvature
    m_controlPoint += QPointF(delta) / 2.0;
}

void ArrowAnnotation::setControlPoint(const QPointF &p)
{
    m_controlPoint = p;
}

void ArrowAnnotation::moveBy(const QPoint &delta)
{
    m_start += delta;
    m_end += delta;
    m_controlPoint += delta;
}

void ArrowAnnotation::translate(const QPointF& delta)
{
    moveBy(delta.toPoint());
}

std::unique_ptr<AnnotationItem> ArrowAnnotation::clone() const
{
    auto cloned = std::make_unique<ArrowAnnotation>(m_start, m_end, m_color, m_width, m_lineEndStyle, m_lineStyle);
    cloned->setControlPoint(m_controlPoint);
    return cloned;
}
