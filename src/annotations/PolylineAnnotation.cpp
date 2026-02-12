#include "annotations/PolylineAnnotation.h"

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
    if (m_points.size() < 2) {
        return;
    }

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

    QPen pen(m_color, m_width, qtStyle, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);  // Ensure no fill for the polyline path
    painter.setRenderHint(QPainter::Antialiasing, true);

    double arrowLength = qMax(10.0, m_width * 3.0);
    int lastIdx = m_points.size() - 1;

    // Check if we have arrowheads
    bool hasEndArrow = (m_lineEndStyle != LineEndStyle::None);
    bool hasStartArrow = (m_lineEndStyle == LineEndStyle::BothArrow ||
                          m_lineEndStyle == LineEndStyle::BothArrowOutline);
    bool needsEndAdjust = (m_lineEndStyle == LineEndStyle::EndArrow ||
                           m_lineEndStyle == LineEndStyle::EndArrowOutline ||
                           m_lineEndStyle == LineEndStyle::BothArrow ||
                           m_lineEndStyle == LineEndStyle::BothArrowOutline);
    bool needsStartAdjust = hasStartArrow;

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
    QPainterPath path;

    // Adjust first point for start arrowhead
    QPointF startPoint = m_points[0];
    if (needsStartAdjust && m_points.size() >= 2) {
        double angle = qAtan2(m_points[0].y() - m_points[1].y(),
                             m_points[0].x() - m_points[1].x());
        startPoint = QPointF(
            m_points[0].x() - arrowLength * qCos(angle),
            m_points[0].y() - arrowLength * qSin(angle)
        );
    }
    path.moveTo(startPoint);

    // Draw segments
    for (int i = 1; i <= lastIdx; ++i) {
        QPointF target = m_points[i];
        
        // Check if this vertex needs adjustment for the end arrow
        if (needsEndAdjust && i == arrowTipIdx) {
             const QPoint& prev = m_points[i - 1];
             double angle = qAtan2(target.y() - prev.y(), target.x() - prev.x());
             target = QPointF(
                target.x() - arrowLength * qCos(angle),
                target.y() - arrowLength * qSin(angle)
             );
        }
        
        path.lineTo(target);
    }

    painter.drawPath(path);

    // Draw arrowheads based on style
    switch (m_lineEndStyle) {
    case LineEndStyle::None:
        break;
    case LineEndStyle::EndArrow:
        drawArrowhead(painter, m_points[arrowTipIdx - 1], m_points[arrowTipIdx], true);
        break;
    case LineEndStyle::EndArrowOutline:
        drawArrowhead(painter, m_points[arrowTipIdx - 1], m_points[arrowTipIdx], false);
        break;
    case LineEndStyle::EndArrowLine:
        drawArrowheadLine(painter, m_points[arrowTipIdx - 1], m_points[arrowTipIdx]);
        break;
    case LineEndStyle::BothArrow:
        drawArrowhead(painter, m_points[arrowTipIdx - 1], m_points[arrowTipIdx], true);
        drawArrowhead(painter, m_points[1], m_points[0], true);
        break;
    case LineEndStyle::BothArrowOutline:
        drawArrowhead(painter, m_points[arrowTipIdx - 1], m_points[arrowTipIdx], false);
        drawArrowhead(painter, m_points[1], m_points[0], false);
        break;
    }

    painter.restore();
}

void PolylineAnnotation::drawArrowhead(QPainter& painter, const QPoint& from, const QPoint& to, bool filled) const
{
    // Calculate the angle of the last segment
    double angle = qAtan2(to.y() - from.y(), to.x() - from.x());

    // Arrowhead size proportional to line width
    double arrowLength = qMax(10.0, m_width * 3.0);
    double arrowAngle = M_PI / 6.0;  // 30 degrees

    // Calculate arrowhead points
    QPointF arrowP1(
        to.x() - arrowLength * qCos(angle - arrowAngle),
        to.y() - arrowLength * qSin(angle - arrowAngle)
    );
    QPointF arrowP2(
        to.x() - arrowLength * qCos(angle + arrowAngle),
        to.y() - arrowLength * qSin(angle + arrowAngle)
    );

    // Draw arrowhead triangle
    QPainterPath arrowPath;
    arrowPath.moveTo(to);
    arrowPath.lineTo(arrowP1);
    arrowPath.lineTo(arrowP2);
    arrowPath.closeSubpath();

    // Use solid pen for arrowhead outline
    QPen solidPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(solidPen);

    if (filled) {
        painter.setBrush(m_color);
    } else {
        painter.setBrush(Qt::NoBrush);
    }
    painter.drawPath(arrowPath);
}

void PolylineAnnotation::drawArrowheadLine(QPainter& painter, const QPoint& from, const QPoint& to) const
{
    // Calculate the angle of the last segment
    double angle = qAtan2(to.y() - from.y(), to.x() - from.x());

    // Arrowhead size proportional to line width
    double arrowLength = qMax(10.0, m_width * 3.0);
    double arrowAngle = M_PI / 6.0;  // 30 degrees

    // Calculate arrowhead points
    QPointF arrowP1(
        to.x() - arrowLength * qCos(angle - arrowAngle),
        to.y() - arrowLength * qSin(angle - arrowAngle)
    );
    QPointF arrowP2(
        to.x() - arrowLength * qCos(angle + arrowAngle),
        to.y() - arrowLength * qSin(angle + arrowAngle)
    );

    // Draw two lines forming a V (no closed path)
    QPen solidPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(solidPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(arrowP1, QPointF(to));
    painter.drawLine(QPointF(to), arrowP2);
}

QRect PolylineAnnotation::boundingRect() const
{
    if (m_points.isEmpty()) {
        return QRect();
    }

    int minX = m_points[0].x();
    int maxX = m_points[0].x();
    int minY = m_points[0].y();
    int maxY = m_points[0].y();

    for (const QPoint& p : m_points) {
        minX = qMin(minX, p.x());
        maxX = qMax(maxX, p.x());
        minY = qMin(minY, p.y());
        maxY = qMax(maxY, p.y());
    }

    // Add margin for line width and arrowhead
    int margin = qMax(20, m_width / 2 + 15);
    return QRect(minX - margin, minY - margin,
                 maxX - minX + 2 * margin, maxY - minY + 2 * margin);
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
    if (m_points.size() < 2) {
        return false;
    }

    // Create a path from the points
    QPainterPath path;
    path.moveTo(m_points[0]);
    for (int i = 1; i < m_points.size(); ++i) {
        path.lineTo(m_points[i]);
    }

    // Use QPainterPathStroker to create a widened path for hit testing
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(10, m_width + 6));  // At least 10px tolerance
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    QPainterPath strokedPath = stroker.createStroke(path);
    return strokedPath.contains(pos);
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
