#include "annotations/PencilStroke.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

// ============================================================================
// Helper: Ramer-Douglas-Peucker path simplification algorithm
// Reduces the number of points while preserving the overall shape
// ============================================================================

static qreal perpendicularDistance(const QPointF& point, const QPointF& lineStart, const QPointF& lineEnd)
{
    qreal dx = lineEnd.x() - lineStart.x();
    qreal dy = lineEnd.y() - lineStart.y();

    qreal lineLengthSq = dx * dx + dy * dy;
    if (lineLengthSq < 1e-10) {
        // Line start and end are the same point
        qreal pdx = point.x() - lineStart.x();
        qreal pdy = point.y() - lineStart.y();
        return qSqrt(pdx * pdx + pdy * pdy);
    }

    // Calculate perpendicular distance using cross product
    qreal t = ((point.x() - lineStart.x()) * dx + (point.y() - lineStart.y()) * dy) / lineLengthSq;
    t = qBound(0.0, t, 1.0);

    qreal projX = lineStart.x() + t * dx;
    qreal projY = lineStart.y() + t * dy;

    qreal distX = point.x() - projX;
    qreal distY = point.y() - projY;

    return qSqrt(distX * distX + distY * distY);
}

static void rdpSimplify(const QVector<QPointF>& points, int start, int end,
                        qreal epsilon, QVector<bool>& keep)
{
    if (end <= start + 1) {
        return;
    }

    qreal maxDist = 0.0;
    int maxIndex = start;

    for (int i = start + 1; i < end; ++i) {
        qreal dist = perpendicularDistance(points[i], points[start], points[end]);
        if (dist > maxDist) {
            maxDist = dist;
            maxIndex = i;
        }
    }

    if (maxDist > epsilon) {
        keep[maxIndex] = true;
        rdpSimplify(points, start, maxIndex, epsilon, keep);
        rdpSimplify(points, maxIndex, end, epsilon, keep);
    }
}

static QVector<QPointF> simplifyPath(const QVector<QPointF>& points, qreal epsilon)
{
    if (points.size() < 3) {
        return points;
    }

    QVector<bool> keep(points.size(), false);
    keep[0] = true;
    keep[points.size() - 1] = true;

    rdpSimplify(points, 0, points.size() - 1, epsilon, keep);

    QVector<QPointF> result;
    result.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            result.append(points[i]);
        }
    }

    return result;
}

// ============================================================================
// Helper: Build smooth path using Catmull-Rom splines converted to cubic Bezier
// Catmull-Rom guarantees C1 continuity (smooth tangents at all control points)
// and interpolates through the original points.
// ============================================================================

static QPainterPath buildSmoothPath(const QVector<QPointF> &inputPoints)
{
    QPainterPath path;

    if (inputPoints.size() < 2) {
        return path;
    }

    // Simplify path to remove redundant points (epsilon = 1.0 pixel)
    QVector<QPointF> points = simplifyPath(inputPoints, 1.0);

    if (points.size() < 2) {
        return path;
    }

    if (points.size() == 2) {
        path.moveTo(points[0]);
        path.lineTo(points[1]);
        return path;
    }

    if (points.size() == 3) {
        path.moveTo(points[0]);
        path.quadTo(points[1], points[2]);
        return path;
    }

    path.moveTo(points[0]);

    // Catmull-Rom to Bezier conversion factor (tension = 1.0)
    constexpr qreal alpha = 1.0 / 6.0;

    for (int i = 0; i < points.size() - 1; ++i) {
        // Get four points for Catmull-Rom segment, using reflection at boundaries
        const QPointF p0 = (i == 0)
            ? points[0] * 2.0 - points[1]
            : points[i - 1];
        const QPointF& p1 = points[i];
        const QPointF& p2 = points[i + 1];
        const QPointF p3 = (i == points.size() - 2)
            ? points[i + 1] * 2.0 - points[i]
            : points[i + 2];

        // Convert to cubic Bezier control points
        QPointF c1 = p1 + alpha * (p2 - p0);
        QPointF c2 = p2 - alpha * (p3 - p1);

        path.cubicTo(c1, c2, p2);
    }

    return path;
}

// ============================================================================
// PencilStroke Implementation
// ============================================================================

PencilStroke::PencilStroke(const QVector<QPointF> &points, const QColor &color, int width,
                           LineStyle lineStyle)
    : m_points(points)
    , m_color(color)
    , m_width(width)
    , m_lineStyle(lineStyle)
{
}

void PencilStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

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
    painter.setBrush(Qt::NoBrush);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path = buildSmoothPath(m_points);
    painter.drawPath(path);

    painter.restore();
}

QRect PencilStroke::boundingRect() const
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
                                    static_cast<int>(maxX - minX) + 2 * margin, static_cast<int>(maxY - minY) + 2 * margin);
        m_boundingRectDirty = false;
    }
    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> PencilStroke::clone() const
{
    return std::make_unique<PencilStroke>(m_points, m_color, m_width, m_lineStyle);
}

void PencilStroke::addPoint(const QPointF &point)
{
    m_points.append(point);

    // Incrementally update bounding rect cache
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

QPainterPath PencilStroke::strokePath() const
{
    if (m_points.size() < 2) {
        return QPainterPath();
    }

    // Build the stroke path with width
    QPainterPath linePath;
    linePath.moveTo(m_points[0]);
    for (int i = 1; i < m_points.size(); ++i) {
        linePath.lineTo(m_points[i]);
    }

    QPainterPathStroker stroker;
    stroker.setWidth(m_width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    return stroker.createStroke(linePath);
}

bool PencilStroke::intersectsCircle(const QPoint &center, int radius) const
{
    // Quick bounding box check first
    QRect bbox = boundingRect();
    QRect eraserRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    if (!bbox.intersects(eraserRect)) {
        return false;
    }

    // Path-based intersection
    QPainterPath eraserPath;
    eraserPath.addEllipse(center, radius, radius);
    return strokePath().intersects(eraserPath);
}
