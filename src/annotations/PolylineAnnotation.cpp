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

    // Build the path for all segments
    QPainterPath path;
    path.moveTo(m_points[0]);

    // For the last segment, adjust endpoint if arrowhead is present
    int lastIdx = m_points.size() - 1;
    for (int i = 1; i < lastIdx; ++i) {
        path.lineTo(m_points[i]);
    }

    // Handle the last segment specially for arrowhead
    if (m_lineEndStyle != LineEndStyle::None && m_points.size() >= 2) {
        // Move line end back to arrowhead base so line doesn't protrude through arrow
        const QPoint& secondLast = m_points[lastIdx - 1];
        const QPoint& last = m_points[lastIdx];

        double angle = qAtan2(last.y() - secondLast.y(), last.x() - secondLast.x());
        double arrowLength = qMax(15.0, m_width * 5.0);
        QPointF lineEnd(
            last.x() - arrowLength * qCos(angle),
            last.y() - arrowLength * qSin(angle)
        );
        path.lineTo(lineEnd);
    } else {
        path.lineTo(m_points[lastIdx]);
    }

    painter.drawPath(path);

    // Draw arrowhead at the end if needed
    if (m_lineEndStyle != LineEndStyle::None && m_points.size() >= 2) {
        drawArrowhead(painter, m_points[lastIdx - 1], m_points[lastIdx]);
    }

    painter.restore();
}

void PolylineAnnotation::drawArrowhead(QPainter& painter, const QPoint& from, const QPoint& to) const
{
    // Calculate the angle of the last segment
    double angle = qAtan2(to.y() - from.y(), to.x() - from.x());

    // Arrowhead size proportional to line width
    double arrowLength = qMax(15.0, m_width * 5.0);
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

    // Draw filled arrowhead
    QPainterPath arrowPath;
    arrowPath.moveTo(to);
    arrowPath.lineTo(arrowP1);
    arrowPath.lineTo(arrowP2);
    arrowPath.closeSubpath();

    painter.setBrush(m_color);
    painter.drawPath(arrowPath);
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

void PolylineAnnotation::removeLastPoint()
{
    if (!m_points.isEmpty()) {
        m_points.removeLast();
    }
}
