#ifndef MOSAICSTROKE_H
#define MOSAICSTROKE_H

#include "AnnotationItem.h"
#include <QVector>
#include <QPoint>
#include <QPixmap>
#include <QImage>

/**
 * @brief Freehand mosaic stroke with classic pixelation (block averaging)
 */
class MosaicStroke : public AnnotationItem
{
public:
    MosaicStroke(const QVector<QPoint> &points, const QPixmap &sourcePixmap,
                 int width = 24, int blockSize = 12);

    void draw(QPainter &painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    void addPoint(const QPoint &point);
    void updateSource(const QPixmap &sourcePixmap);

private:
    QVector<QPoint> m_points;
    QPixmap m_sourcePixmap;
    mutable QImage m_sourceImageCache;
    int m_width;      // Brush width
    int m_blockSize;  // Mosaic block size
    qreal m_devicePixelRatio;

    // Performance optimization: rendered result cache
    mutable QPixmap m_renderedCache;
    mutable int m_cachedPointCount = 0;
    mutable QRect m_cachedBounds;
    mutable qreal m_cachedDpr = 0.0;

    // Pixelated mosaic algorithm aligned to the source image grid
    QImage applyPixelatedMosaic(const QRect &strokeBounds) const;
    QRgb calculateBlockAverageColor(const QImage &image, int x, int y,
                                     int blockW, int blockH) const;
};

#endif // MOSAICSTROKE_H
