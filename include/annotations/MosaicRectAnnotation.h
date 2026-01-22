#ifndef MOSAICRECTANNOTATION_H
#define MOSAICRECTANNOTATION_H

#include "AnnotationItem.h"
#include <QRect>
#include <QPixmap>
#include <QImage>
#include <memory>

// Shared pixmap type for explicit memory sharing across mosaic annotations
using SharedPixmap = std::shared_ptr<const QPixmap>;

/**
 * @brief Rectangular mosaic annotation with pixelation or Gaussian blur effect.
 *
 * Used by auto-blur to cover detected faces with a mosaic overlay.
 * Unlike MosaicStroke (freehand), this fills an entire rectangle.
 */
class MosaicRectAnnotation : public AnnotationItem
{
public:
    enum class BlurType {
        Pixelate,
        Gaussian
    };

    MosaicRectAnnotation(const QRect& rect, SharedPixmap sourcePixmap,
                         int blockSize = 12, BlurType blurType = BlurType::Pixelate);

    void draw(QPainter& painter) const override;
    QRect boundingRect() const override;
    std::unique_ptr<AnnotationItem> clone() const override;

    BlurType blurType() const { return m_blurType; }

private:
    QRect m_rect;           // Rectangle in logical coordinates
    SharedPixmap m_sourcePixmap; // Shared to avoid memory duplication
    int m_blockSize;        // Pixelation block size
    BlurType m_blurType;
    qreal m_devicePixelRatio;

    // Performance cache
    mutable QPixmap m_renderedCache;
    mutable QRect m_cachedRect;
    mutable qreal m_cachedDpr = 0.0;

    QImage applyPixelatedMosaic(qreal dpr) const;
    QImage applyGaussianBlur(qreal dpr) const;
    QRgb calculateBlockAverageColor(const QImage& image, int x, int y, int blockW, int blockH) const;
};

#endif // MOSAICRECTANNOTATION_H
