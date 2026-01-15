#include "annotations/MosaicStroke.h"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QtMath>
#include <algorithm>
#include <opencv2/imgproc.hpp>

// ============================================================================
// MosaicStroke Implementation (classic pixelation)
// ============================================================================

MosaicStroke::MosaicStroke(const QVector<QPoint> &points, const QPixmap &sourcePixmap,
                           int width, int blockSize, BlurType blurType)
    : m_points(points)
    , m_sourcePixmap(sourcePixmap)
    , m_width(width)
    , m_blockSize(blockSize)
    , m_blurType(blurType)
    , m_devicePixelRatio(sourcePixmap.devicePixelRatio())
{
}

QRgb MosaicStroke::calculateBlockAverageColor(const QImage &image, int x, int y,
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
        const QRgb *scanLine = reinterpret_cast<const QRgb*>(image.constScanLine(py));
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

QImage MosaicStroke::applyPixelatedMosaic(const QRect &strokeBounds) const
{
    // Calculate effective block size with DPI scaling
    int effectiveBlockSize = qMax(8, static_cast<int>(m_blockSize * m_devicePixelRatio));

    // Convert stroke bounds to device pixels for sampling
    QRect deviceStrokeBounds(
        static_cast<int>(strokeBounds.x() * m_devicePixelRatio),
        static_cast<int>(strokeBounds.y() * m_devicePixelRatio),
        static_cast<int>(strokeBounds.width() * m_devicePixelRatio),
        static_cast<int>(strokeBounds.height() * m_devicePixelRatio)
    );

    if (deviceStrokeBounds.isEmpty()) {
        return QImage();
    }

    auto floorToBlock = [](int value, int block) {
        int rem = value % block;
        if (rem < 0) {
            rem += block;
        }
        return value - rem;
    };

    int startBlockX = floorToBlock(deviceStrokeBounds.left(), effectiveBlockSize);
    int startBlockY = floorToBlock(deviceStrokeBounds.top(), effectiveBlockSize);
    int endBlockX = deviceStrokeBounds.right();
    int endBlockY = deviceStrokeBounds.bottom();

    // Determine fetch area (aligned blocks)
    QRect blockAlignedRect(
        startBlockX,
        startBlockY,
        endBlockX - startBlockX + effectiveBlockSize,
        endBlockY - startBlockY + effectiveBlockSize
    );
    
    // Intersect with source image bounds
    QRect sourceBounds(0, 0, m_sourcePixmap.width(), m_sourcePixmap.height());
    QRect fetchRect = blockAlignedRect.intersected(sourceBounds);

    if (fetchRect.isEmpty()) {
        return QImage();
    }

    // Fetch ONLY the required patch
    QImage sourcePatch = m_sourcePixmap.copy(fetchRect).toImage().convertToFormat(QImage::Format_ARGB32);

    // Create result image for the stroke bounds region
    QImage resultImage(deviceStrokeBounds.size(), QImage::Format_ARGB32);
    resultImage.fill(Qt::transparent);

    // Process blocks aligned to the full image grid for stable pixelation
    for (int blockY = startBlockY; blockY <= endBlockY; blockY += effectiveBlockSize) {
        for (int blockX = startBlockX; blockX <= endBlockX; blockX += effectiveBlockSize) {
            QRect blockRectGlobal(blockX, blockY, effectiveBlockSize, effectiveBlockSize);
            
            // Check intersection with deviceStrokeBounds (target)
            QRect targetRect = blockRectGlobal.intersected(deviceStrokeBounds);
            if (targetRect.isEmpty()) {
                continue;
            }

            // Check intersection with fetchRect (source data)
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

            int destX = targetRect.x() - deviceStrokeBounds.x();
            int destY = targetRect.y() - deviceStrokeBounds.y();

            for (int py = 0; py < targetRect.height(); ++py) {
                if (destY + py >= resultImage.height()) break;
                QRgb *destLine = reinterpret_cast<QRgb*>(resultImage.scanLine(destY + py));
                int fillWidth = qMin(targetRect.width(), resultImage.width() - destX);
                if (fillWidth > 0 && destX >= 0) {
                    std::fill(destLine + destX, destLine + destX + fillWidth, blockColor);
                }
            }
        }
    }

    return resultImage;
}

QImage MosaicStroke::applyGaussianBlur(const QRect &strokeBounds) const
{
    // Convert stroke bounds to device pixels
    QRect deviceStrokeBounds(
        static_cast<int>(strokeBounds.x() * m_devicePixelRatio),
        static_cast<int>(strokeBounds.y() * m_devicePixelRatio),
        static_cast<int>(strokeBounds.width() * m_devicePixelRatio),
        static_cast<int>(strokeBounds.height() * m_devicePixelRatio)
    );

    if (deviceStrokeBounds.isEmpty()) {
        return QImage();
    }

    // Clamp to image bounds
    QRect sourceBounds(0, 0, m_sourcePixmap.width(), m_sourcePixmap.height());
    QRect clampedBounds = deviceStrokeBounds.intersected(sourceBounds);
    
    if (clampedBounds.isEmpty()) {
        return QImage();
    }

    // Extract region from source (Just the needed part)
    QImage regionImage = m_sourcePixmap.copy(clampedBounds).toImage();
    QImage rgb = regionImage.convertToFormat(QImage::Format_RGB32);

    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);

    // Calculate sigma based on block size (larger block = more blur)
    double sigma = static_cast<double>(m_blockSize) * m_devicePixelRatio / 2.0;
    if (sigma < 1.0) sigma = 1.0;

    // Apply Gaussian blur
    cv::GaussianBlur(bgr, bgr, cv::Size(0, 0), sigma);

    // Convert back to QImage
    cv::Mat bgra;
    cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);

    QImage blurred(bgra.data, bgra.cols, bgra.rows,
                   static_cast<int>(bgra.step), QImage::Format_RGB32);
    blurred = blurred.copy();  // Deep copy

    // Create result image with proper offset
    QImage resultImage(deviceStrokeBounds.size(), QImage::Format_ARGB32);
    resultImage.fill(Qt::transparent);

    // Paint blurred region into result at correct offset
    int offsetX = clampedBounds.x() - deviceStrokeBounds.x();
    int offsetY = clampedBounds.y() - deviceStrokeBounds.y();

    QPainter p(&resultImage);
    p.drawImage(offsetX, offsetY, blurred);
    p.end();

    return resultImage;
}

void MosaicStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

    QRect bounds = boundingRect();
    if (bounds.isEmpty()) return;

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid
    bool cacheValid = !m_renderedCache.isNull()
        && m_cachedPointCount == m_points.size()
        && m_cachedBounds == bounds
        && m_cachedDpr == dpr;

    if (!cacheValid) {
        // Use 2x width for mosaic brush (UI shows half the actual drawing size)
        int effectiveWidth = m_width * 2;
        int halfWidth = effectiveWidth / 2;

        // === Step 1: Create blurred/pixelated effect ===
        QImage mosaicImage;
        switch (m_blurType) {
        case BlurType::Gaussian:
            mosaicImage = applyGaussianBlur(bounds);
            break;
        case BlurType::Pixelate:
        default:
            mosaicImage = applyPixelatedMosaic(bounds);
            break;
        }
        if (mosaicImage.isNull()) {
            m_renderedCache = QPixmap();
            return;
        }

        // === Step 2: Create mask from stroke path (square brush) ===
        QImage maskImage(bounds.size() * dpr, QImage::Format_Grayscale8);
        maskImage.fill(0);  // Start fully transparent

        QPainter maskPainter(&maskImage);
        maskPainter.setRenderHint(QPainter::Antialiasing, false);
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(Qt::white);

        // Interpolation step for gap-free coverage
        int interpolationStep = qMax(1, halfWidth / 2);

        // Draw squares along the stroke path with interpolation
        for (int i = 0; i < m_points.size(); ++i) {
            QPoint pt = m_points[i];
            // Convert to mask coordinates (relative to bounds)
            int mx = static_cast<int>((pt.x() - bounds.left()) * dpr);
            int my = static_cast<int>((pt.y() - bounds.top()) * dpr);
            int size = static_cast<int>(effectiveWidth * dpr);

            maskPainter.fillRect(mx - size / 2, my - size / 2, size, size, Qt::white);

            // Interpolate to next point
            if (i < m_points.size() - 1) {
                QPoint nextPt = m_points[i + 1];
                int dx = nextPt.x() - pt.x();
                int dy = nextPt.y() - pt.y();
                int distSq = dx * dx + dy * dy;

                if (distSq > interpolationStep * interpolationStep) {
                    double dist = qSqrt(static_cast<double>(distSq));
                    int steps = static_cast<int>(dist / interpolationStep);

                    for (int s = 1; s < steps; ++s) {
                        double t = static_cast<double>(s) / steps;
                        int interpX = static_cast<int>((pt.x() + dx * t - bounds.left()) * dpr);
                        int interpY = static_cast<int>((pt.y() + dy * t - bounds.top()) * dpr);
                        maskPainter.fillRect(interpX - size / 2, interpY - size / 2, size, size, Qt::white);
                    }
                }
            }
        }
        maskPainter.end();

        // === Step 3: Composite mosaic image using mask ===
        // Apply mask to mosaic image
        for (int y = 0; y < qMin(mosaicImage.height(), maskImage.height()); ++y) {
            QRgb *pixelRow = reinterpret_cast<QRgb*>(mosaicImage.scanLine(y));
            const uchar *maskRow = maskImage.constScanLine(y);

            for (int x = 0; x < qMin(mosaicImage.width(), maskImage.width()); ++x) {
                int alpha = maskRow[x];  // 0-255 from grayscale mask
                if (alpha == 0) {
                    pixelRow[x] = qRgba(0, 0, 0, 0);  // Fully transparent
                } else {
                    // Keep original color, set alpha from mask
                    QRgb orig = pixelRow[x];
                    pixelRow[x] = qRgba(qRed(orig), qGreen(orig), qBlue(orig), alpha);
                }
            }
        }

        m_renderedCache = QPixmap::fromImage(mosaicImage);
        m_renderedCache.setDevicePixelRatio(dpr);

        m_cachedPointCount = m_points.size();
        m_cachedBounds = bounds;
        m_cachedDpr = dpr;
    }

    // Draw cached result
    painter.drawPixmap(bounds.topLeft(), m_renderedCache);
}

QRect MosaicStroke::boundingRect() const
{
    if (m_points.isEmpty()) return QRect();

    int minX = m_points[0].x();
    int maxX = m_points[0].x();
    int minY = m_points[0].y();
    int maxY = m_points[0].y();

    for (const QPoint &p : m_points) {
        minX = qMin(minX, p.x());
        maxX = qMax(maxX, p.x());
        minY = qMin(minY, p.y());
        maxY = qMax(maxY, p.y());
    }

    // Use 2x width for mosaic brush (UI shows half the actual drawing size)
    int margin = m_width + m_blockSize;
    return QRect(minX - margin, minY - margin,
                 maxX - minX + 2 * margin, maxY - minY + 2 * margin);
}

std::unique_ptr<AnnotationItem> MosaicStroke::clone() const
{
    return std::make_unique<MosaicStroke>(m_points, m_sourcePixmap, m_width, m_blockSize, m_blurType);
}

void MosaicStroke::addPoint(const QPoint &point)
{
    m_points.append(point);
}

void MosaicStroke::updateSource(const QPixmap &sourcePixmap)
{
    m_sourcePixmap = sourcePixmap;
    m_devicePixelRatio = sourcePixmap.devicePixelRatio();
}

void MosaicStroke::setBlurType(BlurType type)
{
    if (m_blurType != type) {
        m_blurType = type;
        m_renderedCache = QPixmap();  // Clear cache to regenerate with new blur type
    }
}

bool MosaicStroke::intersectsCircle(const QPoint &center, int radius) const
{
    if (m_points.size() < 2) {
        return false;
    }

    QRect bbox = boundingRect();
    QRect eraserRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    if (!bbox.intersects(eraserRect)) {
        return false;
    }

    QPainterPath linePath;
    linePath.moveTo(m_points[0]);
    for (int i = 1; i < m_points.size(); ++i) {
        linePath.lineTo(m_points[i]);
    }

    QPainterPathStroker stroker;
    stroker.setWidth(m_width * 2);  // 2x width for mosaic brush
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    QPainterPath strokePath = stroker.createStroke(linePath);

    QPainterPath eraserPath;
    eraserPath.addEllipse(center, radius, radius);
    return strokePath.intersects(eraserPath);
}
