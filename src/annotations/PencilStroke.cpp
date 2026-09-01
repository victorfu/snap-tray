#include "annotations/PencilStroke.h"
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

constexpr qreal kParameterEpsilon = 1e-3;
constexpr int kPreviewTileSize = 256;
constexpr int kPreviewBatchSegments = 64;
constexpr int kMaxPreviewRasterCaches = 4;

qreal parameterStep(const QPointF& a, const QPointF& b)
{
    const qreal dx = b.x() - a.x();
    const qreal dy = b.y() - a.y();
    const qreal distance = qSqrt(dx * dx + dy * dy);
    return qMax(kParameterEpsilon, qSqrt(distance));
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

QPainterPath buildSmoothPath(const QVector<QPointF>& points,
                             int firstSegment = 0,
                             int lastSegmentExclusive = -1)
{
    QPainterPath path;
    if (points.size() < 2 || firstSegment >= points.size() - 1) {
        return path;
    }

    firstSegment = qMax(0, firstSegment);
    const int segmentCount = points.size() - 1;
    if (lastSegmentExclusive < 0) {
        lastSegmentExclusive = segmentCount;
    }
    lastSegmentExclusive = qBound(firstSegment, lastSegmentExclusive, segmentCount);
    if (firstSegment >= lastSegmentExclusive) {
        return path;
    }

    path.moveTo(points[firstSegment]);
    for (int i = firstSegment; i < lastSegmentExclusive; ++i) {
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

Qt::PenStyle penStyleForLineStyle(LineStyle lineStyle)
{
    switch (lineStyle) {
    case LineStyle::Dashed:
        return Qt::DashLine;
    case LineStyle::Dotted:
        return Qt::DotLine;
    case LineStyle::Solid:
    default:
        return Qt::SolidLine;
    }
}

QPen pencilPen(const QColor& color, int width, LineStyle lineStyle)
{
    return QPen(color, width, penStyleForLineStyle(lineStyle), Qt::RoundCap, Qt::RoundJoin);
}

QPair<qreal, qreal> previewDeviceScales(const QPainter& painter)
{
    const QTransform transform = painter.deviceTransform();
    const qreal scaleX = std::hypot(transform.m11(), transform.m12());
    const qreal scaleY = std::hypot(transform.m21(), transform.m22());
    return {scaleX, scaleY};
}

bool supportsExactPreviewRasterTransform(const QPainter& painter,
                                         const QPair<qreal, qreal>& scales)
{
    const QTransform transform = painter.deviceTransform();
    const bool axisAligned =
        qAbs(transform.m12()) < 0.001 && qAbs(transform.m21()) < 0.001;
    const bool uniformScale = qAbs(scales.first - scales.second) < 0.001;
    const bool integralScale =
        qAbs(scales.first - 1.0) < 0.001 || qAbs(scales.first - 2.0) < 0.001;
    return axisAligned && uniformScale && integralScale;
}

int tileIndex(qreal coordinate)
{
    return static_cast<int>(std::floor(coordinate / kPreviewTileSize));
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
    rebuildPathCaches();
}

void PencilStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

    painter.save();

    const QPen pen = pencilPen(m_color, m_width, m_lineStyle);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool requiresSinglePath =
        m_lineStyle != LineStyle::Solid || m_color.alpha() != 255;
    if (requiresSinglePath && !m_finalized) {
        painter.drawPath(buildSmoothPath(m_points));
    } else {
        if (!m_cachedPath.isEmpty()) {
            painter.drawPath(m_cachedPath);
        }

        if (!m_tailPath.isEmpty()) {
            painter.drawPath(m_tailPath);
        }
    }

    painter.restore();
}

void PencilStroke::drawPreview(QPainter &painter) const
{
    if (m_points.size() < 2) {
        return;
    }

    // The sparse coverage cache is bit-stable for the normal opaque solid
    // Pencil. Dashed/dotted phase and translucent self-overlap must retain the
    // canonical single-path compositor semantics, so those modes use the
    // canonical vector path instead of an approximate raster shortcut.
    const QPair<qreal, qreal> deviceScales = previewDeviceScales(painter);
    if (m_lineStyle != LineStyle::Solid || m_color.alpha() != 255 ||
        !supportsExactPreviewRasterTransform(painter, deviceScales)) {
        draw(painter);
        return;
    }

    const qreal scale = deviceScales.first;
    const int scaleKey = qRound(scale * 64.0);
    auto cacheIt = m_previewRasterCaches.find(scaleKey);
    if (cacheIt == m_previewRasterCaches.end()) {
        if (m_previewRasterCaches.size() >= kMaxPreviewRasterCaches) {
            m_previewRasterCaches.erase(m_previewRasterCaches.begin());
        }
        PreviewRasterCache cache;
        cache.rasterScale = scale;
        cacheIt = m_previewRasterCaches.insert(scaleKey, std::move(cache));
    }

    PreviewRasterCache& cache = cacheIt.value();
    syncPreviewRasterCache(cache);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    drawPreviewTiles(painter, cache);

    const QPen basePen = pencilPen(m_color, m_width, m_lineStyle);
    if (cache.renderedSegmentCount < m_cachedSegmentCount) {
        painter.setPen(basePen);
        const QPainterPath pendingPrefix = buildSmoothPath(
            m_points, cache.renderedSegmentCount, m_cachedSegmentCount);
        if (!pendingPrefix.isEmpty()) {
            painter.drawPath(pendingPrefix);
        }
    }
    if (!m_tailPath.isEmpty()) {
        painter.setPen(basePen);
        painter.drawPath(m_tailPath);
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
    auto clone = std::make_unique<PencilStroke>(m_points, m_color, m_width, m_lineStyle);
    if (m_finalized) {
        clone->finalize();
    }
    return clone;
}

std::size_t PencilStroke::estimatedRetainedBytes() const
{
    return sizeof(PencilStroke) +
        static_cast<std::size_t>(m_points.capacity()) * sizeof(QPointF) +
        static_cast<std::size_t>(m_cachedPath.elementCount() +
                                 m_tailPath.elementCount() +
                                 m_previewAffectedPath.elementCount()) *
            sizeof(QPainterPath::Element);
}

void PencilStroke::translate(const QPointF& delta)
{
    if (delta.isNull()) {
        return;
    }

    for (QPointF& point : m_points) {
        point += delta;
    }

    QTransform translation;
    translation.translate(delta.x(), delta.y());
    m_cachedPath = translation.map(m_cachedPath);
    m_tailPath = translation.map(m_tailPath);
    m_previewAffectedPath = translation.map(m_previewAffectedPath);
    m_boundingRectDirty = true;
    m_previewRasterCaches.clear();
}

void PencilStroke::addPoint(const QPointF &point)
{
    m_points.append(point);
    if (m_finalized) {
        m_finalized = false;
        rebuildPathCaches();
    }
    else {
        appendLockableSegments();
        rebuildTailPath();
    }

    // If the cache is already dirty, keep it dirty so the next boundingRect()
    // recomputes from all points, including points provided at construction.
    if (!m_boundingRectDirty) {
        // Appending a point changes the former tail segment and adds one new
        // segment. Union their smooth bounds into the cache; keeping the old
        // tail bounds is conservative and avoids a full-path rebuild.
        const int firstAffectedSegment = qMax(0, m_points.size() - 4);
        m_boundingRectCache = m_boundingRectCache.united(
            smoothPathBounds(m_points, m_width, firstAffectedSegment));
    }
}

void PencilStroke::finalize()
{
    // For styles where separate painter calls change the output, pay the
    // one-time cost of consolidating the incremental prefix/tail caches.
    // Opaque solid strokes keep their existing caches because the split is
    // visually stable and avoids changing the active-stroke raster baseline.
    if (m_lineStyle != LineStyle::Solid || m_color.alpha() != 255) {
        m_cachedPath = buildSmoothPath(m_points);
        m_tailPath = QPainterPath();
        m_cachedSegmentCount = qMax(0, m_points.size() - 1);
    }
    m_previewAffectedPath = QPainterPath();
    m_finalized = true;
    m_previewRasterCaches.clear();
}

QRect PencilStroke::previewAffectedBoundingRect() const
{
    const qreal margin = m_width / 2.0 + 2.0;
    if (!m_previewAffectedPath.isEmpty()) {
        return m_previewAffectedPath.boundingRect()
            .adjusted(-margin, -margin, margin, margin)
            .toAlignedRect();
    }
    if (!m_points.isEmpty()) {
        return QRectF(m_points.last(), m_points.last())
            .adjusted(-margin, -margin, margin, margin)
            .toAlignedRect();
    }
    return {};
}

void PencilStroke::appendCachedSegment(int segmentIndex)
{
    if (segmentIndex < 0 || segmentIndex >= m_points.size() - 1) {
        return;
    }

    if (m_cachedPath.isEmpty()) {
        m_cachedPath.moveTo(m_points[segmentIndex]);
    }

    const QPointF p0 = (segmentIndex == 0)
        ? m_points[0] * 2.0 - m_points[1]
        : m_points[segmentIndex - 1];
    const QPointF& p1 = m_points[segmentIndex];
    const QPointF& p2 = m_points[segmentIndex + 1];
    const QPointF p3 = (segmentIndex == m_points.size() - 2)
        ? m_points[segmentIndex + 1] * 2.0 - m_points[segmentIndex]
        : m_points[segmentIndex + 2];

    appendCentripetalCatmullRomSegment(m_cachedPath, p0, p1, p2, p3);

    ++m_cachedSegmentCount;
}

void PencilStroke::appendLockableSegments()
{
    const int lockableSegments = qMax(0, m_points.size() - 3);
    while (m_cachedSegmentCount < lockableSegments) {
        appendCachedSegment(m_cachedSegmentCount);
    }
}

void PencilStroke::rebuildPathCaches()
{
    m_cachedPath = QPainterPath();
    m_tailPath = QPainterPath();
    m_previewAffectedPath = QPainterPath();
    m_cachedSegmentCount = 0;
    m_previewRasterCaches.clear();
    appendLockableSegments();
    rebuildTailPath();
}

void PencilStroke::rebuildTailPath()
{
    m_tailPath = buildSmoothPath(m_points, m_cachedSegmentCount);
    m_previewAffectedPath = buildSmoothPath(
        m_points, qMax(0, m_cachedSegmentCount - 1));
}

void PencilStroke::syncPreviewRasterCache(PreviewRasterCache& cache) const
{
    const int lockableSegments = qMax(0, m_points.size() - 3);
    const int rasterizableSegments =
        (lockableSegments / kPreviewBatchSegments) * kPreviewBatchSegments;
    const int padding = m_width / 2 + 3;
    const int logicalTileExtent = kPreviewTileSize + padding * 2;
    const QSize physicalTileSize(
        qMax(1, qCeil(logicalTileExtent * cache.rasterScale)),
        qMax(1, qCeil(logicalTileExtent * cache.rasterScale)));

    while (cache.renderedSegmentCount < rasterizableSegments) {
        const int firstSegment = cache.renderedSegmentCount;
        const int lastSegment = qMin(
            firstSegment + kPreviewBatchSegments, rasterizableSegments);
        const QPainterPath batchPath = buildSmoothPath(
            m_points, firstSegment, lastSegment);
        if (batchPath.isEmpty()) {
            break;
        }

        QSet<QPair<int, int>> touchedTiles;
        for (int segment = firstSegment; segment < lastSegment; ++segment) {
            const QPainterPath segmentPath = buildSmoothPath(
                m_points, segment, segment + 1);
            const QRectF segmentBounds = segmentPath.boundingRect().adjusted(
                -padding, -padding, padding, padding);
            const int firstTileX = tileIndex(segmentBounds.left());
            const int lastTileX = tileIndex(segmentBounds.right());
            const int firstTileY = tileIndex(segmentBounds.top());
            const int lastTileY = tileIndex(segmentBounds.bottom());

            for (int tileY = firstTileY; tileY <= lastTileY; ++tileY) {
                for (int tileX = firstTileX; tileX <= lastTileX; ++tileX) {
                    const QRectF paddedTile(
                        tileX * kPreviewTileSize - padding,
                        tileY * kPreviewTileSize - padding,
                        kPreviewTileSize + padding * 2,
                        kPreviewTileSize + padding * 2);
                    if (segmentPath.intersects(paddedTile) ||
                        paddedTile.contains(m_points[segment]) ||
                        paddedTile.contains(m_points[segment + 1])) {
                        touchedTiles.insert(QPair<int, int>(tileX, tileY));
                    }
                }
            }
        }

        QPen coveragePen = pencilPen(Qt::white, m_width, m_lineStyle);

        for (const QPair<int, int>& tileKey : touchedTiles) {
            const int tileX = tileKey.first;
            const int tileY = tileKey.second;
            PreviewTile& tile = cache.tiles[tileKey];
            if (tile.coverage.isNull()) {
                tile.coverage = QImage(physicalTileSize, QImage::Format_Alpha8);
                tile.coverage.setDevicePixelRatio(cache.rasterScale);
                tile.coverage.fill(0);
            }

            QImage scratch(tile.coverage.size(), QImage::Format_Alpha8);
            scratch.setDevicePixelRatio(cache.rasterScale);
            scratch.fill(0);
            {
                QPainter scratchPainter(&scratch);
                scratchPainter.setRenderHint(QPainter::Antialiasing, true);
                scratchPainter.translate(
                    -tileX * kPreviewTileSize + padding,
                    -tileY * kPreviewTileSize + padding);
                scratchPainter.setPen(coveragePen);
                scratchPainter.setBrush(Qt::NoBrush);
                scratchPainter.drawPath(batchPath);
            }

            for (int y = 0; y < tile.coverage.height(); ++y) {
                auto* destination = tile.coverage.scanLine(y);
                const auto* source = scratch.constScanLine(y);
                for (int x = 0; x < tile.coverage.width(); ++x) {
                    destination[x] = qMax(destination[x], source[x]);
                }
            }
            tile.coloredDirty = true;
        }

        cache.renderedSegmentCount = lastSegment;
    }
}

void PencilStroke::drawPreviewTiles(QPainter& painter, PreviewRasterCache& cache) const
{
    const int padding = m_width / 2 + 3;
    const QRectF clipBounds = painter.hasClipping()
        ? painter.clipBoundingRect()
        : QRectF();

    for (auto it = cache.tiles.begin(); it != cache.tiles.end(); ++it) {
        const int tileX = it.key().first;
        const int tileY = it.key().second;
        const QRectF tileCore(
            tileX * kPreviewTileSize,
            tileY * kPreviewTileSize,
            kPreviewTileSize,
            kPreviewTileSize);
        if (clipBounds.isValid() && !clipBounds.isEmpty() &&
            !tileCore.intersects(clipBounds)) {
            continue;
        }

        PreviewTile& tile = it.value();
        if (tile.coloredDirty || tile.colored.isNull()) {
            tile.colored = QImage(
                tile.coverage.size(), QImage::Format_ARGB32_Premultiplied);
            tile.colored.setDevicePixelRatio(cache.rasterScale);
            tile.colored.fill(Qt::transparent);
            for (int y = 0; y < tile.coverage.height(); ++y) {
                const auto* coverage = tile.coverage.constScanLine(y);
                auto* destination = reinterpret_cast<QRgb*>(tile.colored.scanLine(y));
                for (int x = 0; x < tile.coverage.width(); ++x) {
                    const int alpha = qRound(
                        coverage[x] * (m_color.alpha() / 255.0));
                    destination[x] = qPremultiply(qRgba(
                        m_color.red(), m_color.green(), m_color.blue(), alpha));
                }
            }
            tile.coloredDirty = false;
        }

        painter.save();
        painter.setClipRect(tileCore, Qt::IntersectClip);
        painter.drawImage(
            QPointF(tileCore.left() - padding, tileCore.top() - padding),
            tile.colored);
        painter.restore();
    }
}

QPainterPath PencilStroke::strokePath() const
{
    if (m_points.size() < 2) {
        return QPainterPath();
    }

    QPainterPath linePath;
    if (m_lineStyle != LineStyle::Solid || m_color.alpha() != 255) {
        linePath = m_finalized ? m_cachedPath : buildSmoothPath(m_points);
    } else {
        linePath = m_cachedPath;
        if (linePath.isEmpty()) {
            linePath = m_tailPath;
        } else if (!m_tailPath.isEmpty()) {
            linePath.addPath(m_tailPath);
        }
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
