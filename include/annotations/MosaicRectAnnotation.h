#ifndef MOSAICRECTANNOTATION_H
#define MOSAICRECTANNOTATION_H

#include "AnnotationItem.h"
#include <QRect>
#include <QPixmap>
#include <QImage>

/**
 * @brief Rectangular mosaic annotation with pixelation effect.
 *
 * Used by auto-blur to cover detected faces with a mosaic overlay.
 * Unlike MosaicStroke (freehand), this fills an entire rectangle.
 */
class MosaicRectAnnotation : public AnnotationItem
{
public:
    MosaicRectAnnotation(const QRect& rect, const QPixmap& sourcePixmap, int blockSize = 12);

    void draw(QPainter& painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

private:
    QRect m_rect;           // Rectangle in logical coordinates
    QPixmap m_sourcePixmap; // Source image for mosaic sampling
    int m_blockSize;        // Pixelation block size
    qreal m_devicePixelRatio;

    // Performance cache
    mutable QPixmap m_renderedCache;
    mutable QRect m_cachedRect;
    mutable qreal m_cachedDpr = 0.0;
    mutable QImage m_sourceImageCache;

    QImage applyPixelatedMosaic(qreal dpr) const;
    QRgb calculateBlockAverageColor(const QImage& image, int x, int y, int blockW, int blockH) const;
};

#endif // MOSAICRECTANNOTATION_H
