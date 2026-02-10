#include "annotations/MosaicRectAnnotation.h"
#include "utils/MatConverter.h"
#include <QPainter>
#include <QDebug>
#include <algorithm>
#include <opencv2/imgproc.hpp>

// ============================================================================
// MosaicRectAnnotation Implementation (rectangular pixelation for auto-blur)
// ============================================================================

MosaicRectAnnotation::MosaicRectAnnotation(const QRect& rect, SharedPixmap sourcePixmap,
                                           int blockSize, BlurType blurType)
    : m_rect(rect)
    , m_sourcePixmap(std::move(sourcePixmap))
    , m_blockSize(blockSize)
    , m_blurType(blurType)
    , m_devicePixelRatio(m_sourcePixmap ? m_sourcePixmap->devicePixelRatio() : 1.0)
{
}

QRgb MosaicRectAnnotation::calculateBlockAverageColor(const QImage& image, int x, int y,
                                                       int blockW, int blockH) const
{
    // Calculate sampling bounds
    int startX = qBound(0, x, image.width() - 1);
    int startY = qBound(0, y, image.height() - 1);
    int endX = qBound(0, x + blockW, image.width());
    int endY = qBound(0, y + blockH, image.height());

    if (startX >= endX || startY >= endY) {
        return qRgba(128, 128, 128, 255);  // Fallback gray
    }

    // Calculate average color using scanLine for performance
    qint64 totalR = 0, totalG = 0, totalB = 0, totalA = 0;
    int pixelCount = 0;

    for (int py = startY; py < endY; ++py) {
        const QRgb* scanLine = reinterpret_cast<const QRgb*>(image.constScanLine(py));
        for (int px = startX; px < endX; ++px) {
            QRgb pixel = scanLine[px];
            totalR += qRed(pixel);
            totalG += qGreen(pixel);
            totalB += qBlue(pixel);
            totalA += qAlpha(pixel);
            ++pixelCount;
        }
    }

    if (pixelCount == 0) {
        return qRgba(128, 128, 128, 255);
    }

    return qRgba(
        static_cast<int>(totalR / pixelCount),
        static_cast<int>(totalG / pixelCount),
        static_cast<int>(totalB / pixelCount),
        static_cast<int>(totalA / pixelCount)
    );
}

QImage MosaicRectAnnotation::applyPixelatedMosaic(qreal dpr) const
{
    // Use the source pixmap's DPR consistently for both sampling and output
    qreal sourceDpr = m_devicePixelRatio;

    // Calculate effective block size with DPI scaling
    int effectiveBlockSize = qMax(8, static_cast<int>(m_blockSize * sourceDpr));

    // Convert rect bounds to device pixels for sampling from source image
    // m_rect is in logical coordinates, source image is in device pixels
    QRect deviceRect(
        static_cast<int>(m_rect.x() * sourceDpr),
        static_cast<int>(m_rect.y() * sourceDpr),
        static_cast<int>(m_rect.width() * sourceDpr),
        static_cast<int>(m_rect.height() * sourceDpr)
    );

    if (deviceRect.isEmpty()) {
        return QImage();
    }

    // Determine the source region to copy (aligned to blocks)
    // We need to fetch enough context to handle the blocks correctly
    auto floorToBlock = [](int value, int block) {
        int rem = value % block;
        if (rem < 0) {
            rem += block;
        }
        return value - rem;
    };

    int startBlockX = floorToBlock(deviceRect.left(), effectiveBlockSize);
    int startBlockY = floorToBlock(deviceRect.top(), effectiveBlockSize);
    int endBlockX = deviceRect.right();
    int endBlockY = deviceRect.bottom();

    // Calculate the union of all involved blocks
    QRect blockAlignedRect(
        startBlockX,
        startBlockY,
        endBlockX - startBlockX + effectiveBlockSize, // Add one extra block size to be safe
        endBlockY - startBlockY + effectiveBlockSize
    );
    
    // Intersect with source image bounds
    if (!m_sourcePixmap) {
        return QImage();
    }
    QRect sourceBounds(0, 0, m_sourcePixmap->width(), m_sourcePixmap->height());
    QRect fetchRect = blockAlignedRect.intersected(sourceBounds);

    if (fetchRect.isEmpty()) {
        return QImage();
    }

    // Fetch ONLY the required part of the image
    // This is the key optimization: avoiding full screen conversion
    QImage sourcePatch = m_sourcePixmap->copy(fetchRect).toImage().convertToFormat(QImage::Format_ARGB32);

    // Create result image in device pixels (matching deviceRect size)
    QImage resultImage(deviceRect.size(), QImage::Format_ARGB32);
    resultImage.fill(Qt::transparent);

    // Process blocks aligned to the full image grid for stable pixelation
    // Iterate in GLOBAL coordinates (relative to full image)
    for (int blockY = startBlockY; blockY <= endBlockY; blockY += effectiveBlockSize) {
        for (int blockX = startBlockX; blockX <= endBlockX; blockX += effectiveBlockSize) {
            // Check intersection with deviceRect (target area)
            QRect blockRectGlobal(blockX, blockY, effectiveBlockSize, effectiveBlockSize);
            QRect targetRect = blockRectGlobal.intersected(deviceRect);
            if (targetRect.isEmpty()) {
                continue;
            }

            // Check intersection with fetchRect (source data available)
            QRect sampleRectGlobal = blockRectGlobal.intersected(fetchRect);
            if (sampleRectGlobal.isEmpty()) {
                continue;
            }
            
            // Calculate local coordinates for sampling from sourcePatch
            int localSampleX = sampleRectGlobal.x() - fetchRect.x();
            int localSampleY = sampleRectGlobal.y() - fetchRect.y();

            QRgb blockColor = calculateBlockAverageColor(
                sourcePatch,
                localSampleX,
                localSampleY,
                sampleRectGlobal.width(),
                sampleRectGlobal.height()
            );

            int destX = targetRect.x() - deviceRect.x();
            int destY = targetRect.y() - deviceRect.y();

            for (int py = 0; py < targetRect.height(); ++py) {
                if (destY + py >= resultImage.height()) break;
                QRgb* destLine = reinterpret_cast<QRgb*>(resultImage.scanLine(destY + py));
                int fillWidth = qMin(targetRect.width(), resultImage.width() - destX);
                if (fillWidth > 0 && destX >= 0) {
                    std::fill(destLine + destX, destLine + destX + fillWidth, blockColor);
                }
            }
        }
    }

    return resultImage;
}

QImage MosaicRectAnnotation::applyGaussianBlur(qreal dpr) const
{
    qreal sourceDpr = m_devicePixelRatio;

    // Convert rect bounds to device pixels
    QRect deviceRect(
        static_cast<int>(m_rect.x() * sourceDpr),
        static_cast<int>(m_rect.y() * sourceDpr),
        static_cast<int>(m_rect.width() * sourceDpr),
        static_cast<int>(m_rect.height() * sourceDpr)
    );

    if (deviceRect.isEmpty()) {
        return QImage();
    }

    // Clamp to image bounds
    if (!m_sourcePixmap) {
        return QImage();
    }
    QRect sourceBounds(0, 0, m_sourcePixmap->width(), m_sourcePixmap->height());
    QRect clampedRect = deviceRect.intersected(sourceBounds);

    if (clampedRect.isEmpty()) {
        return QImage();
    }

    // Extract region from source (Avoid full image conversion)
    // fetchRect is same as clampedRect because for Gaussian blur we just need the content
    // Note: Technically for proper edge blurring we might need a margin,
    // but clampedRect is usually sufficient for visual purposes or we could expand it slightly.
    // For simplicity and correctness with existing logic, we use the intersection.
    QImage regionImage = m_sourcePixmap->copy(clampedRect).toImage();
    QImage rgb = regionImage.convertToFormat(QImage::Format_RGB32);

    // Calculate sigma based on block size (larger block = more blur)
    double sigma = static_cast<double>(m_blockSize) * sourceDpr / 2.0;
    if (sigma < 1.0) {
        sigma = 1.0;
    }

    cv::Mat mat = MatConverter::toMat(rgb);
    cv::GaussianBlur(mat, mat, cv::Size(0, 0), sigma);

    // Copy blurred result (detaches from original buffer)
    QImage blurred = rgb.copy();

    // Create result image with proper offset
    QImage resultImage(deviceRect.size(), QImage::Format_ARGB32);
    resultImage.fill(Qt::transparent);

    // Paint blurred region into result at correct offset
    int offsetX = clampedRect.x() - deviceRect.x();
    int offsetY = clampedRect.y() - deviceRect.y();

    QPainter p(&resultImage);
    p.drawImage(offsetX, offsetY, blurred);
    p.end();

    return resultImage;
}

void MosaicRectAnnotation::draw(QPainter& painter) const
{
    if (m_rect.isEmpty()) return;

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid - use source DPR since that's what we render with
    bool cacheValid = !m_renderedCache.isNull()
        && m_cachedRect == m_rect
        && m_cachedDpr == m_devicePixelRatio;

    if (!cacheValid) {
        QImage mosaicImage;
        switch (m_blurType) {
        case BlurType::Gaussian:
            mosaicImage = applyGaussianBlur(dpr);
            break;
        case BlurType::Pixelate:
        default:
            mosaicImage = applyPixelatedMosaic(dpr);
            break;
        }
        if (mosaicImage.isNull()) {
            m_renderedCache = QPixmap();
            return;
        }

        m_renderedCache = QPixmap::fromImage(mosaicImage);
        // Use source DPR so Qt scales correctly when drawing
        m_renderedCache.setDevicePixelRatio(m_devicePixelRatio);

        m_cachedRect = m_rect;
        m_cachedDpr = m_devicePixelRatio;
    }

    // Draw cached result at logical coordinates
    painter.drawPixmap(m_rect.topLeft(), m_renderedCache);
}

QRect MosaicRectAnnotation::boundingRect() const
{
    return m_rect;
}

std::unique_ptr<AnnotationItem> MosaicRectAnnotation::clone() const
{
    // SharedPixmap copy is cheap - just increments reference count
    return std::make_unique<MosaicRectAnnotation>(m_rect, m_sourcePixmap, m_blockSize, m_blurType);
}

