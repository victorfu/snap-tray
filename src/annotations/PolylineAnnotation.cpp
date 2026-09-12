#include "annotations/PolylineAnnotation.h"
#include "annotations/LineAnnotationGeometry.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {
constexpr qreal kMinChordLength = 1e-6;
}

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
    QVector<QPointF> points;
    for (const QPoint& point : m_points) {
        if (!points.isEmpty() && points.last() == point) continue;
        while (points.size() >= 2) {
            const QPointF incoming = points.last() - points[points.size() - 2];
            const QPointF outgoing = QPointF(point) - points.last();
            if (incoming.x() * outgoing.y() != incoming.y() * outgoing.x()
                || QPointF::dotProduct(incoming, outgoing) <= 0.0) break;
            // Rendering-only normalization: splitting a straight segment must
            // not perturb head placement through floating-point accumulation.
            points.removeLast();
        }
        points.append(point);
    }
    if (points.size() < 2) {
        return geometry;
    }
    QVector<qreal> distances{0.0};
    qreal totalLength = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        totalLength += QLineF(points[i - 1], points[i]).length();
        distances.append(totalLength);
    }

    const auto pointAtDistance = [&](qreal distance) {
        for (int i = 1; i < points.size(); ++i) {
            if (distance <= distances[i]) {
                const qreal fraction = (distance - distances[i - 1])
                    / (distances[i] - distances[i - 1]);
                return points[i - 1] + fraction * (points[i] - points[i - 1]);
            }
        }
        return points.last();
    };
    const bool hasEnd = m_lineEndStyle != LineEndStyle::None;
    const bool hasStart = m_lineEndStyle == LineEndStyle::BothArrow
        || m_lineEndStyle == LineEndStyle::BothArrowOutline;
    const qreal headDistance = qMin(LineAnnotationGeometry::headBaseDistance(m_width),
                                    hasStart ? totalLength / 2.0 : totalLength);
    qreal startDistance = 0.0;
    qreal endDistance = totalLength;

    if (hasEnd) {
        qreal baseDistance = totalLength - headDistance;
        QPointF base = pointAtDistance(baseDistance);
        if (QLineF(base, points.last()).length() < kMinChordLength) {
            // A looping tail can return to the tip. Use its last nonzero segment
            // rather than inventing a direction for a zero-length chord.
            baseDistance = qMax(baseDistance, distances[distances.size() - 2]);
            base = pointAtDistance(baseDistance);
        }
        geometry.addHeadFromBase(points.last(), base, m_width, m_lineEndStyle);
        if (m_lineEndStyle != LineEndStyle::EndArrowLine) {
            endDistance = baseDistance;
        }
    }
    if (hasStart) {
        startDistance = headDistance;
        QPointF base = pointAtDistance(startDistance);
        if (QLineF(base, points.first()).length() < kMinChordLength) {
            startDistance = qMin(startDistance, distances[1]);
            base = pointAtDistance(startDistance);
        }
        geometry.addHeadFromBase(points.first(), base, m_width, m_lineEndStyle);
    }

    // Trim along the polyline, consuming short terminal segments instead of
    // extending them backwards. Each head joins the retained shaft at its base
    // and points to the actual endpoint, even when its neck spans a corner.
    geometry.capStyle = hasEnd ? Qt::FlatCap : Qt::RoundCap;
    if (endDistance > startDistance) {
        geometry.shaft.moveTo(pointAtDistance(startDistance));
        for (int i = 1; i < points.size() - 1; ++i) {
            if (distances[i] > startDistance && distances[i] < endDistance) {
                geometry.shaft.lineTo(points[i]);
            }
        }
        geometry.shaft.lineTo(pointAtDistance(endDistance));
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
