#ifndef MOSAICSTROKE_H
#define MOSAICSTROKE_H

#include "MosaicBlurType.h"
#include "AnnotationItem.h"
#include <QVector>
#include <QPoint>
#include <QPixmap>
#include <QImage>
#include <memory>

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
    void setSourcePixmap(SharedPixmap pixmap);

    void addPoint(const QPoint &point);
    QVector<QPoint> points() const { return m_points; }
    int width() const { return m_width; }
    int blockSize() const { return m_blockSize; }
    BlurType blurType() const { return m_blurType; }

    // Collision detection for eraser (path-based intersection)
    bool intersectsCircle(const QPoint &center, int radius) const;

private:
    QVector<QPoint> m_points;
    SharedPixmap m_sourcePixmap;  // Shared to avoid memory duplication
    int m_width;      // Brush width
    int m_blockSize;  // Mosaic block size
    BlurType m_blurType;
    qreal m_devicePixelRatio;

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
