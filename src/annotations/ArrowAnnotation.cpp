#include "annotations/ArrowAnnotation.h"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QtMath>
#include <QDebug>

ArrowAnnotation::ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width,
                                 LineEndStyle style, LineStyle lineStyle)
    : m_start(start)
    , m_end(end)
    , m_controlPoint((start + end) / 2)  // Default: midpoint = straight line
    , m_color(color)
    , m_width(width)
    , m_lineEndStyle(style)
    , m_lineStyle(lineStyle)
{
}

double ArrowAnnotation::endTangentAngle() const
{
    // Tangent at t=1 for Quadratic Bézier is proportional to (P2 - P1) = (end - control)
    QPoint tangent = m_end - m_controlPoint;

    // Edge case: if end == control, fall back to end - start
    if (tangent.isNull()) {
        tangent = m_end - m_start;
    }

    return qAtan2(tangent.y(), tangent.x());
}

double ArrowAnnotation::startTangentAngle() const
{
    // Tangent at t=0 for Quadratic Bézier is proportional to (P1 - P0) = (control - start)
    QPoint tangent = m_controlPoint - m_start;

    // Edge case: if control == start, fall back to end - start
    if (tangent.isNull()) {
        tangent = m_end - m_start;
    }

    return qAtan2(tangent.y(), tangent.x());
}

void ArrowAnnotation::draw(QPainter &painter) const
{
    painter.save();

    // Map LineStyle to Qt::PenStyle
    Qt::PenStyle qtStyle = Qt::SolidLine;
    switch (m_lineStyle) {
    case LineStyle::Solid:
        qtStyle = Qt::SolidLine;
        break;
    case LineStyle::Dashed:
        qtStyle = Qt::DashLine;
        break;
    case LineStyle::Dotted:
        qtStyle = Qt::DotLine;
        break;
    }

    QPen pen(m_color, m_width, qtStyle, Qt::FlatCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double arrowLength = qMax(10.0, m_width * 3.0);
    double arrowAngle = M_PI / 6.0;  // 30 degrees
    // Distance from tip to triangle base along the line direction
    double baseDistance = arrowLength * qCos(arrowAngle) - 1.0;

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
    painter.setBrush(Qt::NoBrush);
    QPainterPath curvePath;
    curvePath.moveTo(curveStart);
    curvePath.quadTo(adjustedControl, curveEnd);
    painter.drawPath(curvePath);

    // Draw arrowhead(s) based on line end style
    switch (m_lineEndStyle) {
    case LineEndStyle::None:
        // Plain line, no arrowheads
        break;
    case LineEndStyle::EndArrow:
        drawArrowheadAtAngle(painter, m_end, endTangentAngle(), true);
        break;
    case LineEndStyle::EndArrowOutline:
        drawArrowheadAtAngle(painter, m_end, endTangentAngle(), false);
        break;
    case LineEndStyle::EndArrowLine:
        drawArrowheadLineAtAngle(painter, m_end, endTangentAngle());
        break;
    case LineEndStyle::BothArrow:
        drawArrowheadAtAngle(painter, m_end, endTangentAngle(), true);
        // Start arrow points opposite direction (add PI to reverse)
        drawArrowheadAtAngle(painter, m_start, startTangentAngle() + M_PI, true);
        break;
    case LineEndStyle::BothArrowOutline:
        drawArrowheadAtAngle(painter, m_end, endTangentAngle(), false);
        drawArrowheadAtAngle(painter, m_start, startTangentAngle() + M_PI, false);
        break;
    }

    painter.restore();
}

void ArrowAnnotation::drawArrowheadAtAngle(QPainter &painter, const QPoint &tip, double angle, bool filled) const
{
    // Arrowhead size proportional to line width
    double arrowLength = qMax(10.0, m_width * 3.0);
    double arrowAngle = M_PI / 6.0;  // 30 degrees

    // Calculate arrowhead points
    QPointF arrowP1(
        tip.x() - arrowLength * qCos(angle - arrowAngle),
        tip.y() - arrowLength * qSin(angle - arrowAngle)
    );
    QPointF arrowP2(
        tip.x() - arrowLength * qCos(angle + arrowAngle),
        tip.y() - arrowLength * qSin(angle + arrowAngle)
    );

    // Draw arrowhead triangle
    QPainterPath arrowPath;
    arrowPath.moveTo(tip);
    arrowPath.lineTo(arrowP1);
    arrowPath.lineTo(arrowP2);
    arrowPath.closeSubpath();

    if (filled) {
        // Solid fill without outline stroke
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
    } else {
        // Outline only
        QPen solidPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(solidPen);
        painter.setBrush(Qt::NoBrush);
    }
    painter.drawPath(arrowPath);
}

void ArrowAnnotation::drawArrowheadLineAtAngle(QPainter &painter, const QPoint &tip, double angle) const
{
    // Arrowhead size proportional to line width
    double arrowLength = qMax(10.0, m_width * 3.0);
    double arrowAngle = M_PI / 6.0;  // 30 degrees

    // Calculate arrowhead points
    QPointF arrowP1(
        tip.x() - arrowLength * qCos(angle - arrowAngle),
        tip.y() - arrowLength * qSin(angle - arrowAngle)
    );
    QPointF arrowP2(
        tip.x() - arrowLength * qCos(angle + arrowAngle),
        tip.y() - arrowLength * qSin(angle + arrowAngle)
    );

    // Draw two lines forming a V (no closed path)
    QPen solidPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(solidPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(arrowP1, QPointF(tip));
    painter.drawLine(QPointF(tip), arrowP2);
}

QRect ArrowAnnotation::boundingRect() const
{
    int margin = 20;  // Extra margin for arrowhead

    // Include all three points (start, end, control) in bounding calculation
    int minX = qMin(qMin(m_start.x(), m_end.x()), m_controlPoint.x()) - margin;
    int maxX = qMax(qMax(m_start.x(), m_end.x()), m_controlPoint.x()) + margin;
    int minY = qMin(qMin(m_start.y(), m_end.y()), m_controlPoint.y()) - margin;
    int maxY = qMax(qMax(m_start.y(), m_end.y()), m_controlPoint.y()) + margin;

    return QRect(minX, minY, maxX - minX, maxY - minY);
}

bool ArrowAnnotation::containsPoint(const QPoint &pos) const
{
    qDebug() << "ArrowAnnotation::containsPoint checking pos:" << pos
             << "against start:" << m_start << "end:" << m_end
             << "control:" << m_controlPoint << "width:" << m_width;

    // Create the Bézier curve path
    QPainterPath curvePath;
    curvePath.moveTo(m_start);
    curvePath.quadTo(m_controlPoint, m_end);

    // Use QPainterPathStroker to create a widened path for hit testing
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(kHitTolerance, m_width + 6));  // At least 10px tolerance
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    QPainterPath strokedPath = stroker.createStroke(curvePath);
    bool result = strokedPath.contains(pos);
    qDebug() << "  containsPoint result:" << result << "bounding:" << strokedPath.boundingRect();
    return result;
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
    m_controlPoint += delta / 2;
}

void ArrowAnnotation::setEnd(const QPoint &end)
{
    // When moving end, also move control point to maintain curve shape
    QPoint delta = end - m_end;
    m_end = end;

    // Move control point by half the delta to keep relative curvature
    m_controlPoint += delta / 2;
}

void ArrowAnnotation::setControlPoint(const QPoint &p)
{
    m_controlPoint = p;
}

void ArrowAnnotation::moveBy(const QPoint &delta)
{
    m_start += delta;
    m_end += delta;
    m_controlPoint += delta;
}

std::unique_ptr<AnnotationItem> ArrowAnnotation::clone() const
{
    auto cloned = std::make_unique<ArrowAnnotation>(m_start, m_end, m_color, m_width, m_lineEndStyle, m_lineStyle);
    cloned->setControlPoint(m_controlPoint);
    return cloned;
}
