#include "annotations/MarkerStroke.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

// ============================================================================
// Helper: Build smooth path using quadratic Bezier with midpoints
// This approach uses midpoints as curve endpoints and original points as
// control points, creating smooth curves without overshoot.
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

    // Start at first point
    path.moveTo(points[0]);

    // First segment: line to first midpoint (smooth start)
    QPointF mid0 = (points[0] + points[1]) / 2.0;
    path.lineTo(mid0);

    // Middle segments: midpoint to midpoint with original points as control
    for (int i = 1; i < points.size() - 1; ++i) {
        QPointF mid = (points[i] + points[i + 1]) / 2.0;
        path.quadTo(points[i], mid);
    }

    // Last segment: curve to end point using last point as control
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

void MarkerStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

    painter.save();

    QRect bounds = boundingRect();
    if (bounds.isEmpty()) {
        painter.restore();
        return;
    }

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid
    bool cacheValid = !m_cachedPixmap.isNull()
        && m_cachedDpr == dpr
        && m_cachedPointCount == m_points.size();

    if (!cacheValid) {
        // Regenerate cache
        QPixmap offscreen(bounds.size() * dpr);
        offscreen.setDevicePixelRatio(dpr);
        offscreen.fill(Qt::transparent);

        {
            QPainter offPainter(&offscreen);
            offPainter.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            offPainter.setPen(pen);
            offPainter.setBrush(Qt::NoBrush);

            // Build smooth path and translate to local coordinates
            QPainterPath path = buildSmoothPath(m_points);
            path.translate(-bounds.topLeft());
            offPainter.drawPath(path);
        }

        // Update cache
        m_cachedPixmap = offscreen;
        m_cachedOrigin = bounds.topLeft();
        m_cachedDpr = dpr;
        m_cachedPointCount = m_points.size();
    }

    // Use cached pixmap
    painter.setOpacity(0.4);
    painter.drawPixmap(m_cachedOrigin, m_cachedPixmap);

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

        int margin = m_width / 2 + 1;
        m_boundingRectCache = QRect(static_cast<int>(minX) - margin, static_cast<int>(minY) - margin,
                                    static_cast<int>(maxX - minX) + 2 * margin, static_cast<int>(maxY - minY) + 2 * margin);
        m_boundingRectDirty = false;
    }
    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> MarkerStroke::clone() const
{
    return std::make_unique<MarkerStroke>(m_points, m_color, m_width);
}

void MarkerStroke::addPoint(const QPointF &point)
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
