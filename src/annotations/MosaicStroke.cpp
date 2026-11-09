#include "annotations/MosaicStroke.h"
#include "utils/CoordinateHelper.h"
#include "utils/MatConverter.h"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QtMath>
#include <algorithm>
#include <opencv2/imgproc.hpp>

// ============================================================================
// MosaicStroke Implementation (classic pixelation)
// ============================================================================

MosaicStroke::MosaicStroke(const QVector<QPoint> &points, SharedPixmap sourcePixmap,
                           int width, int blockSize, BlurType blurType)
    : m_points(points)
    , m_sourcePixmap(std::move(sourcePixmap))
    , m_width(width)
    , m_blockSize(blockSize)
    , m_blurType(blurType)
    , m_devicePixelRatio(m_sourcePixmap ? m_sourcePixmap->devicePixelRatio() : 1.0)
{
    for (const QPoint& point : m_points) {
        m_pointBounds |= QRect(point, QSize(1, 1));
    }
    if (m_devicePixelRatio <= 0.0) {
        m_devicePixelRatio = 1.0;
    }
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
    const int effectiveBlockSize = qMax(
        8, CoordinateHelper::toPhysical(QSize(m_blockSize, m_blockSize), m_devicePixelRatio).width());

    // Convert stroke bounds to device pixels for sampling
    const QRect deviceStrokeBounds = CoordinateHelper::toPhysical(strokeBounds, m_devicePixelRatio);

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
    if (!m_sourcePixmap) {
        return QImage();
    }
    QRect sourceBounds(0, 0, m_sourcePixmap->width(), m_sourcePixmap->height());
    QRect fetchRect = blockAlignedRect.intersected(sourceBounds);

    if (fetchRect.isEmpty()) {
        return QImage();
    }

    // Fetch ONLY the required patch
    QImage sourcePatch = m_sourcePixmap->copy(fetchRect).toImage().convertToFormat(QImage::Format_ARGB32);

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
    const QRect deviceStrokeBounds = CoordinateHelper::toPhysical(strokeBounds, m_devicePixelRatio);

    if (deviceStrokeBounds.isEmpty()) {
        return QImage();
    }

    // Clamp to image bounds
    if (!m_sourcePixmap) {
        return QImage();
    }
    QRect sourceBounds(0, 0, m_sourcePixmap->width(), m_sourcePixmap->height());
    QRect clampedBounds = deviceStrokeBounds.intersected(sourceBounds);

    if (clampedBounds.isEmpty()) {
        return QImage();
    }

    // Extract region from source (Just the needed part)
    QImage regionImage = m_sourcePixmap->copy(clampedBounds).toImage();
    QImage rgb = regionImage.convertToFormat(QImage::Format_RGB32);
    // The patch and result canvas use physical pixels. Copied pixmaps retain
    // their DPR, which would shrink the patch during drawImage below. Restore
    // the source DPR only on the final rendered cache in draw().
    rgb.setDevicePixelRatio(1.0);

    // Calculate sigma based on physical block size (larger block = more blur)
    const int physicalBlockSize = CoordinateHelper::toPhysical(
        QSize(m_blockSize, m_blockSize), m_devicePixelRatio).width();
    double sigma = static_cast<double>(physicalBlockSize) / 2.0;
    if (sigma < 1.0) {
        sigma = 1.0;
    }

    cv::Mat mat = MatConverter::toMat(rgb);
    if (mat.empty()) {
        return {};
    }
    cv::GaussianBlur(mat, mat, cv::Size(0, 0), sigma);

    // Copy blurred result (detaches from original buffer)
    QImage blurred = rgb.copy();

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

    if (m_blurType == BlurType::Pixelate && updatePixelateTiles()) {
        drawPixelateTiles(painter, bounds);
        return;
    }

    // The stroke points and source sampling both live in the source pixmap's
    // logical coordinate system. Build the effect and mask at that same DPR;
    // QPainter will scale the resulting pixmap to the destination device DPR.
    // Mixing the painter DPR with the source DPR clips or shrinks the mask when
    // a pin moves between screens with different scale factors.
    const qreal dpr = m_devicePixelRatio > 0.0 ? m_devicePixelRatio : 1.0;

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
        QImage maskImage(mosaicImage.size(), QImage::Format_Grayscale8);
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
            const QPoint maskPos = CoordinateHelper::toPhysical(pt - bounds.topLeft(), dpr);
            const int mx = maskPos.x();
            const int my = maskPos.y();
            const int size = CoordinateHelper::toPhysical(QSize(effectiveWidth, effectiveWidth), dpr).width();

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
                        const QPointF interpPoint(
                            pt.x() + dx * t - bounds.left(),
                            pt.y() + dy * t - bounds.top());
                        const QPoint interpPos = CoordinateHelper::toPhysical(interpPoint, dpr);
                        const int interpX = interpPos.x();
                        const int interpY = interpPos.y();
                        maskPainter.fillRect(interpX - size / 2, interpY - size / 2, size, size, Qt::white);
                    }
                }
            }
        }
        maskPainter.end();

        // === Step 3: Composite mosaic image using mask ===
        // Apply mask to mosaic image
        Q_ASSERT(mosaicImage.size() == maskImage.size());
        if (mosaicImage.size() != maskImage.size()) {
            m_renderedCache = QPixmap();
            return;
        }

        for (int y = 0; y < mosaicImage.height(); ++y) {
            QRgb *pixelRow = reinterpret_cast<QRgb*>(mosaicImage.scanLine(y));
            const uchar *maskRow = maskImage.constScanLine(y);

            for (int x = 0; x < mosaicImage.width(); ++x) {
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
    painter.drawPixmap(bounds, m_renderedCache);
    if (m_blurType == BlurType::Pixelate) {
        // Budget overflow is a compatibility fallback, not an unbounded cache.
        m_renderedCache = QPixmap();
    }
}

QRect MosaicStroke::boundingRect() const
{
    if (m_points.isEmpty()) return QRect();

    const int margin = m_width + m_blockSize;
    return QRect(m_pointBounds.left() - margin, m_pointBounds.top() - margin,
                 m_pointBounds.width() - 1 + 2 * margin,
                 m_pointBounds.height() - 1 + 2 * margin);
}

std::unique_ptr<AnnotationItem> MosaicStroke::clone() const
{
    // SharedPixmap copy is cheap - just increments reference count
    return std::make_unique<MosaicStroke>(m_points, m_sourcePixmap, m_width, m_blockSize, m_blurType);
}

void MosaicStroke::addPoint(const QPoint &point)
{
    // Keep the first duplicate: a single point does not draw, whereas a
    // press/move at the same integer position historically does.
    if (m_points.size() >= 2 && m_points.constLast() == point) return;
    m_points.append(point);
    m_pointBounds |= QRect(point, QSize(1, 1));
}

void MosaicStroke::translate(const QPointF& delta)
{
    QPoint d = delta.toPoint();
    if (d.isNull()) return;
    m_pointBounds.translate(d);
    resetPixelateMask();
    m_tileBudgetExceeded = false;
    for (QPoint& point : m_points) {
        point += d;
    }
}

void MosaicStroke::setSourcePixmap(SharedPixmap pixmap)
{
    resetPixelateMask();
    m_blockColors.clear();
    m_tileBudgetExceeded = false;
    m_sourcePixmap = std::move(pixmap);
    m_devicePixelRatio = m_sourcePixmap ? m_sourcePixmap->devicePixelRatio() : 1.0;
    if (m_devicePixelRatio <= 0.0) {
        m_devicePixelRatio = 1.0;
    }

    m_renderedCache = QPixmap();
    m_cachedPointCount = 0;
    m_cachedBounds = QRect();
    m_cachedDpr = 0.0;
}

QPainterPath MosaicStroke::strokePath() const
{
    if (m_points.size() < 2) {
        return {};
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
    return stroker.createStroke(linePath);
}

bool MosaicStroke::intersectsCircle(const QPoint &center, int radius) const
{
    QRect eraserRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    if (!boundingRect().intersects(eraserRect)) {
        return false;
    }

    QPainterPath eraserPath;
    eraserPath.addEllipse(center, radius, radius);
    return strokePath().intersects(eraserPath);
}

namespace {
int floorToGrid(int value, int grid)
{
    const int remainder = value % grid;
    return value - (remainder < 0 ? remainder + grid : remainder);
}
quint64 tileKey(int x, int y)
{
    return (quint64(quint32(x)) << 32) | quint32(y);
}
QPoint tileOrigin(quint64 key)
{
    return QPoint(qint32(key >> 32), qint32(key & 0xffffffff));
}
}

std::size_t MosaicStroke::pixelateCacheBytes() const
{
    return std::size_t(m_tiles.size()) * kTileCost
        + std::size_t(m_blockColors.totalCost());
}

std::size_t MosaicStroke::estimatedRetainedBytes() const
{
    return sizeof(MosaicStroke) + std::size_t(m_points.capacity()) * sizeof(QPoint)
        + estimatedPixmapBytes(m_renderedCache) + pixelateCacheBytes();
}

void MosaicStroke::resetPixelateMask() const
{
    m_tiles.clear();
    m_tilePointCount = 0;
    m_tileBounds = {};
    m_requiresFullPreview = true;
}

bool MosaicStroke::pixelMappingChanged(const QRect& before, const QRect& after) const
{
    if (before.isEmpty()) return true;
    if (before == after) return false;
    // At integral source DPR all origin translations and dimensions are exact.
    // Fractional DPR can change old mask rounding AND the final image scale.
    // Keep the legacy contract by conservatively rebuilding on bounds changes.
    return m_devicePixelRatio != qFloor(m_devicePixelRatio);
}

QRect MosaicStroke::previewAffectedBoundingRect() const
{
    const QRect bounds = boundingRect();
    if (m_blurType != BlurType::Pixelate || m_requiresFullPreview || m_tilePointCount == 0
        || m_tileBudgetExceeded || pixelMappingChanged(m_tileBounds, bounds)) {
        return bounds.united(m_tileBounds);
    }
    QRect affected;
    for (int i = qMax(0, m_tilePointCount - 1); i < m_points.size(); ++i) {
        affected |= QRect(m_points[i], QSize(1, 1));
    }
    // Includes stamp rounding and resampling at a different destination DPR.
    return affected.adjusted(-m_width - 2, -m_width - 2, m_width + 2, m_width + 2);
}

QRgb MosaicStroke::pixelateBlockColor(int x, int y, int blockSize) const
{
    const quint64 key = tileKey(x, y);
    if (auto* cached = m_blockColors.object(key)) return *cached;
    QRgb color = qRgb(0, 0, 0);
    const QRect sample = QRect(x, y, blockSize, blockSize).intersected(m_sourcePixmap->rect());
    if (!sample.isEmpty()) {
        const QImage patch = m_sourcePixmap->copy(sample).toImage().convertToFormat(QImage::Format_ARGB32);
        color = calculateBlockAverageColor(patch, 0, 0, patch.width(), patch.height());
        // Legacy composition replaces alpha with the binary square mask,
        // including transparent source pixels and blocks outside the source.
        color = qRgb(qRed(color), qGreen(color), qBlue(color));
        ++m_renderWork.sampledBlocks;
    }
    m_blockColors.insert(key, new QRgb(color), sizeof(QRgb) + kEntryOverhead);
    return color;
}

bool MosaicStroke::stampPixelate(const QPointF& point, const QRect& bounds) const
{
    const qreal dpr = m_devicePixelRatio;
    const QRect physicalBounds = CoordinateHelper::toPhysical(bounds, dpr);
    const QPoint center = CoordinateHelper::toPhysical(point - bounds.topLeft(), dpr)
        + physicalBounds.topLeft();
    const int size = CoordinateHelper::toPhysical(QSize(m_width * 2, m_width * 2), dpr).width();
    const QRect stamp = QRect(center - QPoint(size / 2, size / 2), QSize(size, size))
        .intersected(physicalBounds);
    ++m_renderWork.stamps;
    if (stamp.isEmpty()) return true;
    const int blockSize = qMax(8, CoordinateHelper::toPhysical(QSize(m_blockSize, m_blockSize), dpr).width());
    for (int ty = floorToGrid(stamp.top(), kTileSize); ty <= stamp.bottom(); ty += kTileSize) {
        for (int tx = floorToGrid(stamp.left(), kTileSize); tx <= stamp.right(); tx += kTileSize) {
            const quint64 key = tileKey(tx, ty);
            auto it = m_tiles.find(key);
            if (it == m_tiles.end()) {
                if (m_tiles.size() >= kTileBudgetBytes / kTileCost) return false;
                QImage tile(kTileSize, kTileSize, QImage::Format_ARGB32_Premultiplied);
                tile.fill(Qt::transparent);
                it = m_tiles.insert(key, tile);
            }
            const QRect area = stamp.intersected(QRect(tx, ty, kTileSize, kTileSize));
            for (int by = floorToGrid(area.top(), blockSize); by <= area.bottom(); by += blockSize) {
                for (int bx = floorToGrid(area.left(), blockSize); bx <= area.right(); bx += blockSize) {
                    const QRgb color = pixelateBlockColor(bx, by, blockSize);
                    const QRect fill = area.intersected(QRect(bx, by, blockSize, blockSize));
                    for (int y = fill.top(); y <= fill.bottom(); ++y) {
                        auto* row = reinterpret_cast<QRgb*>(it.value().scanLine(y - ty));
                        std::fill(row + fill.left() - tx, row + fill.right() - tx + 1, color);
                    }
                }
            }
            ++m_renderWork.tileWrites;
        }
    }
    return true;
}

bool MosaicStroke::updatePixelateTiles() const
{
    if (m_tileBudgetExceeded) return false;
    const QRect bounds = boundingRect();
    // A size change affects the final resampling phase (and dirty bounds),
    // but only an origin change can move already-stamped mask pixels.
    const bool fractionalOriginChanged = m_devicePixelRatio != qFloor(m_devicePixelRatio)
        && m_tileBounds.topLeft() != bounds.topLeft();
    if (m_tileBounds.isEmpty() || fractionalOriginChanged) {
        resetPixelateMask();
        ++m_renderWork.maskRebuilds;
    }
    if (!m_sourcePixmap || m_sourcePixmap->isNull()) return true;
    const int step = qMax(1, m_width / 2);
    // Revisit only the previous endpoint to interpolate its new outgoing edge.
    for (int i = qMax(0, m_tilePointCount - 1); i < m_points.size(); ++i) {
        if (m_tilePointCount == m_points.size()) break;
        if (!stampPixelate(m_points[i], bounds)) {
            m_tileBudgetExceeded = true;
            resetPixelateMask();
            return false;
        }
        if (i + 1 == m_points.size()) continue;
        const QPoint delta = m_points[i + 1] - m_points[i];
        const double distanceSquared = double(delta.x()) * delta.x() + double(delta.y()) * delta.y();
        if (distanceSquared <= double(step) * step) continue;
        const int steps = int(qSqrt(distanceSquared) / step);
        for (int j = 1; j < steps; ++j) {
            const double t = double(j) / steps;
            if (!stampPixelate(QPointF(m_points[i].x() + delta.x() * t,
                                      m_points[i].y() + delta.y() * t), bounds)) {
                m_tileBudgetExceeded = true;
                resetPixelateMask();
                return false;
            }
        }
    }
    m_tilePointCount = m_points.size();
    m_tileBounds = bounds;
    return true;
}

void MosaicStroke::drawPixelateTiles(QPainter& painter, const QRect& bounds) const
{
    if (!m_sourcePixmap || m_sourcePixmap->isNull() || m_tiles.isEmpty()) return;
    const QRect physical = CoordinateHelper::toPhysical(bounds, m_devicePixelRatio);
    const int blockSize = qMax(8, CoordinateHelper::toPhysical(QSize(m_blockSize, m_blockSize), m_devicePixelRatio).width());
    // The legacy effect is entirely empty when its aligned fetch misses source.
    const int left = floorToGrid(physical.left(), blockSize);
    const int top = floorToGrid(physical.top(), blockSize);
    const QRect fetch(left, top, physical.right() - left + blockSize,
                      physical.bottom() - top + blockSize);
    if (!fetch.intersects(m_sourcePixmap->rect())) return;

    const QTransform transform = painter.deviceTransform();
    const bool nativePixels = painter.compositionMode() == QPainter::CompositionMode_SourceOver
        && qFuzzyCompare(transform.m11(), m_devicePixelRatio)
        && qFuzzyCompare(transform.m22(), m_devicePixelRatio)
        && transform.m12() == 0 && transform.m21() == 0
        && transform.dx() == qRound(transform.dx()) && transform.dy() == qRound(transform.dy())
        && m_devicePixelRatio == qFloor(m_devicePixelRatio)
        && !painter.testRenderHint(QPainter::SmoothPixmapTransform);
    // The single-image mixed-DPR resampling can change pixels far from a new
    // segment even at integral source DPR. The next event must repaint them.
    m_requiresFullPreview = !nativePixels;
    if (nativePixels) {
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
            const QPoint origin = tileOrigin(it.key());
            const QRectF target(QPointF(origin) / m_devicePixelRatio,
                                QSizeF(kTileSize / m_devicePixelRatio, kTileSize / m_devicePixelRatio));
            painter.drawImage(target, it.value());
        }
        return;
    }
    // Preserve the exact single-image resampling phase and filtering across
    // tile boundaries for mixed DPR, fractional bounds, zoom and rotation.
    // This temporary assembly does not resample source blocks or replay masks.
    QImage image(physical.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter assembly(&image);
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
            assembly.drawImage(tileOrigin(it.key()) - physical.topLeft(), it.value());
        }
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(m_devicePixelRatio);
    painter.drawPixmap(bounds, pixmap);
}
