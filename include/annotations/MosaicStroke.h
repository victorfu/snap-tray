#ifndef MOSAICSTROKE_H
#define MOSAICSTROKE_H

#include "MosaicBlurType.h"
#include "AnnotationItem.h"
#include <QVector>
#include <QPoint>
#include <QPixmap>
#include <QImage>
#include <memory>
#include <QCache>
#include <QHash>

// Shared pixmap type for explicit memory sharing across mosaic annotations
using SharedPixmap = std::shared_ptr<const QPixmap>;

/**
 * @brief Freehand mosaic stroke with pixelation or Gaussian blur
 */
class MosaicStroke : public AnnotationItem
{
public:
    using BlurType = MosaicBlurType;

    MosaicStroke(const QVector<QPoint> &points, SharedPixmap sourcePixmap,
                 int width = 24, int blockSize = 12, BlurType blurType = BlurType::Pixelate);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;
    void translate(const QPointF& delta) override;
    std::size_t estimatedRetainedBytes() const override;

    // Per-stroke budgets include a conservative metadata charge per entry.
    // Overflow uses the legacy renderer without retaining its result.
    static constexpr int kTileSize = 128;
    static constexpr int kTileBudgetBytes = 16 * 1024 * 1024;
    static constexpr int kBlockBudgetBytes = 256 * 1024;
    struct RenderWork {
        quint64 sampledBlocks = 0;
        quint64 stamps = 0;
        quint64 tileWrites = 0;
        quint64 maskRebuilds = 0;
    };
    RenderWork renderWork() const { return m_renderWork; }
    std::size_t pixelateCacheBytes() const;
    // All pending additions since the last draw, including a conservative full
    // invalidation when the legacy fractional-DPR mapping changes.
    QRect previewAffectedBoundingRect() const;
    void setSourcePixmap(SharedPixmap pixmap);

    void addPoint(const QPoint &point);
    QVector<QPoint> points() const { return m_points; }
    int width() const { return m_width; }
    int blockSize() const { return m_blockSize; }
    BlurType blurType() const { return m_blurType; }

    // Collision detection for eraser (path-based intersection)
    bool intersectsCircle(const QPoint &center, int radius) const;
    QPainterPath strokePath() const;

private:
    QVector<QPoint> m_points;
    SharedPixmap m_sourcePixmap;  // Shared to avoid memory duplication
    int m_width;      // Brush width
    int m_blockSize;  // Mosaic block size
    BlurType m_blurType;
    qreal m_devicePixelRatio;

    QRect m_pointBounds;
    mutable QHash<quint64, QImage> m_tiles;
    static constexpr int kEntryOverhead = 128;
    static constexpr int kTileCost = kTileSize * kTileSize * sizeof(QRgb) + kEntryOverhead;
    mutable QCache<quint64, QRgb> m_blockColors{kBlockBudgetBytes};
    mutable int m_tilePointCount = 0;
    mutable QRect m_tileBounds;
    mutable bool m_tileBudgetExceeded = false;
    mutable bool m_requiresFullPreview = true;
    mutable RenderWork m_renderWork;
    bool updatePixelateTiles() const;
    bool stampPixelate(const QPointF& point, const QRect& bounds) const;
    QRgb pixelateBlockColor(int x, int y, int blockSize) const;
    void drawPixelateTiles(QPainter& painter, const QRect& bounds) const;
    void resetPixelateMask() const;
    bool pixelMappingChanged(const QRect& before, const QRect& after) const;

    // Performance optimization: rendered result cache
    mutable QPixmap m_renderedCache;
    mutable int m_cachedPointCount = 0;
    mutable QRect m_cachedBounds;
    mutable qreal m_cachedDpr = 0.0;

    // Pixelated mosaic algorithm aligned to the source image grid
    QImage applyPixelatedMosaic(const QRect &strokeBounds) const;
    // Gaussian blur algorithm
    QImage applyGaussianBlur(const QRect &strokeBounds) const;
    QRgb calculateBlockAverageColor(const QImage &image, int x, int y,
                                     int blockW, int blockH) const;
};

#endif // MOSAICSTROKE_H
