#include "annotations/PencilStroke.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace {

constexpr qreal kCatmullRomAlpha = 0.5;
constexpr qreal kParameterEpsilon = 1e-3;

qreal parameterStep(const QPointF& a, const QPointF& b)
{
    const qreal dx = b.x() - a.x();
    const qreal dy = b.y() - a.y();
    const qreal distance = qSqrt(dx * dx + dy * dy);
    return qMax(kParameterEpsilon, qPow(distance, kCatmullRomAlpha));
}

QPointF safeDivide(const QPointF& value, qreal divisor)
{
    return value / qMax(divisor, kParameterEpsilon);
}

// ============================================================================
// Helper: Append a single centripetal Catmull-Rom segment as cubic Bezier.
// This keeps the freehand line stable under uneven sample spacing, which is
// common on Windows fractional-DPI input paths.
// ============================================================================
void appendCentripetalCatmullRomSegment(QPainterPath& path,
                                        const QPointF& p0,
                                        const QPointF& p1,
                                        const QPointF& p2,
                                        const QPointF& p3)
{
    const qreal t0 = 0.0;
    const qreal t1 = t0 + parameterStep(p0, p1);
    const qreal t2 = t1 + parameterStep(p1, p2);
    const qreal t3 = t2 + parameterStep(p2, p3);
    const qreal segmentSpan = t2 - t1;

    if (segmentSpan <= kParameterEpsilon) {
        path.lineTo(p2);
        return;
    }

    const QPointF m1 = segmentSpan * (
        safeDivide(p1 - p0, t1 - t0) -
        safeDivide(p2 - p0, t2 - t0) +
        safeDivide(p2 - p1, t2 - t1));
    const QPointF m2 = segmentSpan * (
        safeDivide(p2 - p1, t2 - t1) -
        safeDivide(p3 - p1, t3 - t1) +
        safeDivide(p3 - p2, t3 - t2));

    const QPointF c1 = p1 + m1 / 3.0;
    const QPointF c2 = p2 - m2 / 3.0;
    path.cubicTo(c1, c2, p2);
}

QPainterPath buildSmoothPath(const QVector<QPointF>& points, int firstSegment = 0)
{
    QPainterPath path;
    if (points.size() < 2 || firstSegment >= points.size() - 1) {
        return path;
    }

    firstSegment = qMax(0, firstSegment);
    path.moveTo(points[firstSegment]);
    for (int i = firstSegment; i < points.size() - 1; ++i) {
        const QPointF p0 = (i == 0)
            ? points[0] * 2.0 - points[1]
            : points[i - 1];
        const QPointF& p1 = points[i];
        const QPointF& p2 = points[i + 1];
        const QPointF p3 = (i == points.size() - 2)
            ? points[i + 1] * 2.0 - points[i]
            : points[i + 2];

        appendCentripetalCatmullRomSegment(path, p0, p1, p2, p3);
    }

    return path;
}

QRect smoothPathBounds(const QVector<QPointF>& points, int width, int firstSegment = 0)
{
    if (points.isEmpty()) {
        return {};
    }

    const QRectF centerlineBounds = points.size() == 1
        ? QRectF(points.first(), points.first())
        : buildSmoothPath(points, firstSegment).boundingRect();
    const int margin = width / 2 + 1;
    return centerlineBounds.toAlignedRect().adjusted(-margin, -margin, margin, margin);
}

} // namespace

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

    // Draw cached (locked) segments
    if (!m_cachedPath.isEmpty()) {
        painter.drawPath(m_cachedPath);
    }

    // Build and draw tail (unlocked segments that may still change)
    int tailStart = m_cachedSegmentCount;
    int pointCount = m_points.size();

    if (tailStart < pointCount - 1) {
        const QPainterPath tailPath = buildSmoothPath(m_points, tailStart);
        painter.drawPath(tailPath);
    }

    painter.restore();
}

QRect PencilStroke::boundingRect() const
{
    if (m_points.isEmpty()) return QRect();

    if (m_boundingRectDirty) {
        m_boundingRectCache = smoothPathBounds(m_points, m_width);
        m_boundingRectDirty = false;
    }
    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> PencilStroke::clone() const
{
    return std::make_unique<PencilStroke>(m_points, m_color, m_width, m_lineStyle);
}

void PencilStroke::translate(const QPointF& delta)
{
    if (delta.isNull()) {
        return;
    }

    for (QPointF& point : m_points) {
        point += delta;
    }

    // Translation invalidates cached geometry derived from old coordinates.
    m_boundingRectDirty = true;
    m_cachedPath = QPainterPath();
    m_cachedSegmentCount = 0;
}

void PencilStroke::addPoint(const QPointF &point)
{
    m_points.append(point);

    // Incrementally lock segments that now have all 4 control points known
    // Segment i depends on points[i-1], points[i], points[i+1], points[i+2]
    // So segment i is locked when we have points[i+2]
    int pointCount = m_points.size();
    int lockableSegments = std::max(0, pointCount - 3);

    while (m_cachedSegmentCount < lockableSegments) {
        int i = m_cachedSegmentCount;

        // First segment: start the path
        if (i == 0) {
            m_cachedPath.moveTo(m_points[0]);
        }

        // Build segment i using 4 control points
        const QPointF p0 = (i == 0)
            ? m_points[0] * 2.0 - m_points[1]  // Reflect for first segment
            : m_points[i - 1];
        const QPointF &p1 = m_points[i];
        const QPointF &p2 = m_points[i + 1];
        const QPointF &p3 = m_points[i + 2];  // Guaranteed to exist

        appendCentripetalCatmullRomSegment(m_cachedPath, p0, p1, p2, p3);
        m_cachedSegmentCount++;
    }

    // If the cache is already dirty, keep it dirty so the next boundingRect()
    // recomputes from all points, including points provided at construction.
    if (!m_boundingRectDirty) {
        // Appending a point changes the former tail segment and adds one new
        // segment. Union their smooth bounds into the cache; keeping the old
        // tail bounds is conservative and avoids a full-path rebuild.
        const int firstAffectedSegment = qMax(0, m_points.size() - 3);
        m_boundingRectCache = m_boundingRectCache.united(
            smoothPathBounds(m_points, m_width, firstAffectedSegment));
    }
}

void PencilStroke::finalize()
{
    // No longer needed - path is stable due to incremental segment locking
}

QPainterPath PencilStroke::strokePath() const
{
    if (m_points.size() < 2) {
        return QPainterPath();
    }

    // Recreate the same cached/tail subpaths used by draw(). Keeping the
    // subpath boundary preserves the round-cap geometry at their shared seam.
    QPainterPath linePath = m_cachedPath;
    const QPainterPath tailPath = buildSmoothPath(m_points, m_cachedSegmentCount);
    if (linePath.isEmpty()) {
        linePath = tailPath;
    } else if (!tailPath.isEmpty()) {
        linePath.addPath(tailPath);
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
