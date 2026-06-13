#include "annotations/MarkerStroke.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

// ============================================================================
// Helper: Build smooth path using quadratic Bezier with midpoints.
// This keeps the marker stroke smooth without the Catmull-Rom overshoot
// that can create visible gaps inside semi-transparent wide strokes.
// ============================================================================

static QPainterPath buildSmoothPath(const QVector<QPointF> &points)
{
    QPainterPath path;

    if (points.size() < 2) {
        return path;
    }

    if (points.size() == 2) {
        path.moveTo(points[0]);
        path.lineTo(points[1]);
        return path;
    }

    // Start at first point.
    path.moveTo(points[0]);

    // First segment: line to the first midpoint for a smooth start.
    QPointF mid0 = (points[0] + points[1]) / 2.0;
    path.lineTo(mid0);

    // Middle segments: midpoint-to-midpoint quadratic curves.
    for (int i = 1; i < points.size() - 1; ++i) {
        QPointF mid = (points[i] + points[i + 1]) / 2.0;
        path.quadTo(points[i], mid);
    }

    // Final segment ends at the last input point.
    path.quadTo(points[points.size() - 1], points.last());

    return path;
}

// ============================================================================
// MarkerStroke Implementation
// ============================================================================

MarkerStroke::MarkerStroke(const QVector<QPointF> &points, const QColor &color, int width)
    : m_points(points)
    , m_color(color)
    , m_width(width)
{
}

void MarkerStroke::rebuildPreviewPath() const
{
    m_cachedPreviewPath = QPainterPath();
    m_cachedPreviewLastControlIndex = 0;

    if (m_points.size() < 3) {
        return;
    }

    m_cachedPreviewPath.moveTo(m_points[0]);
    const QPointF firstMid = (m_points[0] + m_points[1]) / 2.0;
    m_cachedPreviewPath.lineTo(firstMid);

    for (int i = 1; i < m_points.size() - 1; ++i) {
        const QPointF mid = (m_points[i] + m_points[i + 1]) / 2.0;
        m_cachedPreviewPath.quadTo(m_points[i], mid);
        m_cachedPreviewLastControlIndex = i;
    }
}

void MarkerStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.setOpacity(0.4);
    painter.drawPath(buildSmoothPath(m_points));

    painter.restore();
}

void MarkerStroke::drawPreview(QPainter &painter) const
{
    if (m_points.size() < 2) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.setOpacity(0.4);

    if (m_points.size() == 2) {
        painter.drawLine(m_points[0], m_points[1]);
        painter.restore();
        return;
    }

    if (m_cachedPreviewPath.isEmpty() ||
        m_cachedPreviewLastControlIndex != m_points.size() - 2) {
        rebuildPreviewPath();
    }

    if (!m_cachedPreviewPath.isEmpty()) {
        QPainterPath previewPath = m_cachedPreviewPath;
        previewPath.quadTo(m_points.last(), m_points.last());
        painter.drawPath(previewPath);
    }

    painter.restore();
}

QRect MarkerStroke::boundingRect() const
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

        const int margin = m_width / 2 + 1;
        const QRect pointBounds = QRectF(
            QPointF(minX, minY),
            QPointF(maxX, maxY)).normalized().toAlignedRect();
        m_boundingRectCache = pointBounds.adjusted(-margin, -margin, margin, margin);
        m_boundingRectDirty = false;
    }
    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> MarkerStroke::clone() const
{
    return std::unique_ptr<AnnotationItem>(new MarkerStroke(m_points, m_color, m_width));
}

void MarkerStroke::translate(const QPointF& delta)
{
    if (delta.isNull()) {
        return;
    }

    for (QPointF& point : m_points) {
        point += delta;
    }

    // Translation changes both geometry and rendered placement.
    m_boundingRectDirty = true;
    m_cachedPreviewPath = QPainterPath();
    m_cachedPreviewLastControlIndex = 0;
}

void MarkerStroke::addPoint(const QPointF &point)
{
    m_points.append(point);

    // If the cache is already dirty, keep it dirty so the next boundingRect()
    // recomputes from all points, including points provided at construction.
    if (!m_boundingRectDirty) {
        const int margin = m_width / 2 + 1;
        const QRect pointRect = QRectF(point, point)
            .normalized()
            .toAlignedRect()
            .adjusted(-margin, -margin, margin, margin);
        m_boundingRectCache = m_boundingRectCache.united(pointRect);
    }

    if (m_points.size() >= 3) {
        if (m_cachedPreviewPath.isEmpty() ||
            m_cachedPreviewLastControlIndex + 1 != m_points.size() - 2) {
            rebuildPreviewPath();
        } else {
            const int controlIndex = m_points.size() - 2;
            const QPointF mid = (m_points[controlIndex] + m_points[controlIndex + 1]) / 2.0;
            m_cachedPreviewPath.quadTo(m_points[controlIndex], mid);
            m_cachedPreviewLastControlIndex = controlIndex;
        }
    }
}

QPainterPath MarkerStroke::strokePath() const
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

bool MarkerStroke::intersectsCircle(const QPoint &center, int radius) const
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
