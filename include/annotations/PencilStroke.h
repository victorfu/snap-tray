#ifndef PENCILSTROKE_H
#define PENCILSTROKE_H

#include "AnnotationItem.h"
#include "LineStyle.h"
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QImage>
#include <QMap>
#include <QPainterPath>
#include <QRectF>

/**
 * @brief Freehand pencil stroke annotation
 */
class PencilStroke : public AnnotationItem
{
public:
    PencilStroke(const QVector<QPointF> &points, const QColor &color, int width,
                 LineStyle lineStyle = LineStyle::Solid);

    void draw(QPainter &painter) const override;
    void drawPreview(QPainter &painter) const;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;
    void translate(const QPointF& delta) override;
    std::size_t estimatedRetainedBytes() const override;

    void addPoint(const QPointF &point);
    void finalize();
    // Geometry that can change on the next point: newly locked segment + live tail.
    QRect previewAffectedBoundingRect() const;
    const QVector<QPointF>& points() const { return m_points; }
    QColor color() const { return m_color; }
    int width() const { return m_width; }
    LineStyle lineStyle() const { return m_lineStyle; }

    // Collision detection for eraser (path-based intersection)
    bool intersectsCircle(const QPoint &center, int radius) const;
    QPainterPath strokePath() const;

private:
    struct PreviewTile {
        QImage coverage;
        QImage colored;
        bool coloredDirty = true;
    };

    struct PreviewRasterCache {
        qreal rasterScale = 1.0;
        int renderedSegmentCount = 0;
        QMap<QPair<int, int>, PreviewTile> tiles;
    };

    QVector<QPointF> m_points;
    QColor m_color;
    int m_width;
    LineStyle m_lineStyle;

    // Incremental Catmull-Rom spline caching
    mutable QPainterPath m_cachedPath;       // Locked segments that won't change
    mutable QPainterPath m_tailPath;         // Stable allocation for the live tail
    mutable QPainterPath m_previewAffectedPath; // Newly locked segment plus live tail
    mutable int m_cachedSegmentCount = 0;    // Number of segments in cached path
    bool m_finalized = false;

    // Active-stroke preview cache. Completed strokes clear this in finalize().
    // Each render scale keeps sparse logical tiles for the immutable prefix.
    mutable QMap<int, PreviewRasterCache> m_previewRasterCaches;

    // Performance optimization: cached bounding rect
    mutable QRect m_boundingRectCache;
    mutable bool m_boundingRectDirty = true;

    void appendCachedSegment(int segmentIndex);
    void appendLockableSegments();
    void rebuildPathCaches();
    void rebuildTailPath();
    void syncPreviewRasterCache(PreviewRasterCache& cache) const;
    void drawPreviewTiles(QPainter& painter, PreviewRasterCache& cache) const;
};

#endif // PENCILSTROKE_H
