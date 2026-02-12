#include "annotations/PencilStroke.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

// ============================================================================
// Helper: Append a single Catmull-Rom segment as cubic Bezier
// Catmull-Rom guarantees C1 continuity (smooth tangents at all control points)
// ============================================================================

static void appendCatmullRomSegment(QPainterPath &path,
    const QPointF &p0, const QPointF &p1,
    const QPointF &p2, const QPointF &p3)
{
    // Catmull-Rom to Bezier conversion factor (tension = 1.0)
    constexpr qreal alpha = 1.0 / 6.0;
    QPointF c1 = p1 + alpha * (p2 - p0);
    QPointF c2 = p2 - alpha * (p3 - p1);
    path.cubicTo(c1, c2, p2);
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

    // Draw cached (locked) segments
    if (!m_cachedPath.isEmpty()) {
        painter.drawPath(m_cachedPath);
    }

    // Build and draw tail (unlocked segments that may still change)
    int tailStart = m_cachedSegmentCount;
    int pointCount = m_points.size();

    if (tailStart < pointCount - 1) {
        QPainterPath tailPath;

        // MoveTo: connect to cached path endpoint or start fresh
        if (tailStart == 0) {
            tailPath.moveTo(m_points[0]);
        } else {
            tailPath.moveTo(m_points[tailStart]);
        }

        for (int i = tailStart; i < pointCount - 1; ++i) {
            const QPointF p0 = (i == 0)
                ? m_points[0] * 2.0 - m_points[1]
                : m_points[i - 1];
            const QPointF &p1 = m_points[i];
            const QPointF &p2 = m_points[i + 1];
            const QPointF p3 = (i == pointCount - 2)
                ? m_points[i + 1] * 2.0 - m_points[i]  // Reflect for last segment
                : m_points[i + 2];

            appendCatmullRomSegment(tailPath, p0, p1, p2, p3);
        }

        painter.drawPath(tailPath);
    }

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

        appendCatmullRomSegment(m_cachedPath, p0, p1, p2, p3);
        m_cachedSegmentCount++;
    }

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

void PencilStroke::finalize()
{
    // No longer needed - path is stable due to incremental segment locking
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
