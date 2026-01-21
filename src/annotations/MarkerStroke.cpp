#include "annotations/MarkerStroke.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

// ============================================================================
// Helper: Build smooth path using Catmull-Rom splines converted to cubic Bezier
// Catmull-Rom guarantees C1 continuity (smooth tangents at all control points)
// and interpolates through the original points.
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
    bool cacheValid = !m_cachedImage.isNull()
        && m_cachedDpr == dpr
        && m_cachedPointCount == m_points.size();

    if (!cacheValid) {
        // Regenerate cache
        // Use QImage to avoid QPixmap halo/blending artifacts on transparent backgrounds
        QImage offscreen(bounds.size() * dpr, QImage::Format_ARGB32_Premultiplied);
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
        m_cachedImage = offscreen;
        m_cachedOrigin = bounds.topLeft();
        m_cachedDpr = dpr;
        m_cachedPointCount = m_points.size();
    }

    // Use cached image
    painter.setOpacity(0.4);
    painter.drawImage(m_cachedOrigin, m_cachedImage);

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
