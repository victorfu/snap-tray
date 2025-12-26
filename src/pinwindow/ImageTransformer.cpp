#include "pinwindow/ImageTransformer.h"

#include <QTransform>
#include <QDebug>

ImageTransformer::ImageTransformer(QObject* parent)
    : QObject(parent)
{
}

void ImageTransformer::setOriginalPixmap(const QPixmap& pixmap)
{
    m_originalPixmap = pixmap;
    invalidateCache();
}

void ImageTransformer::rotateRight()
{
    m_rotationAngle = (m_rotationAngle + 90) % 360;
    m_displayCacheValid = false;
    emit transformChanged();
    qDebug() << "ImageTransformer: Rotated right, angle now" << m_rotationAngle;
}

void ImageTransformer::rotateLeft()
{
    m_rotationAngle = (m_rotationAngle + 270) % 360;  // +270 is same as -90
    m_displayCacheValid = false;
    emit transformChanged();
    qDebug() << "ImageTransformer: Rotated left, angle now" << m_rotationAngle;
}

void ImageTransformer::flipHorizontal()
{
    m_flipHorizontal = !m_flipHorizontal;
    m_displayCacheValid = false;
    emit transformChanged();
    qDebug() << "ImageTransformer: Flipped horizontal, now" << m_flipHorizontal;
}

void ImageTransformer::flipVertical()
{
    m_flipVertical = !m_flipVertical;
    m_displayCacheValid = false;
    emit transformChanged();
    qDebug() << "ImageTransformer: Flipped vertical, now" << m_flipVertical;
}

void ImageTransformer::setZoomLevel(qreal zoom)
{
    qreal newZoom = qBound(0.1, zoom, 5.0);
    if (!qFuzzyCompare(m_zoomLevel, newZoom)) {
        m_zoomLevel = newZoom;
        m_displayCacheValid = false;
        emit transformChanged();
    }
}

void ImageTransformer::setSmoothing(bool enabled)
{
    if (m_smoothing != enabled) {
        m_smoothing = enabled;
        m_displayCacheValid = false;
        emit transformChanged();
    }
}

void ImageTransformer::setFastScaling(bool fast)
{
    if (m_fastScaling != fast) {
        m_fastScaling = fast;
        m_displayCacheValid = false;
    }
}

void ImageTransformer::invalidateCache()
{
    m_cachedRotation = -1;
    m_transformedCache = QPixmap();
    m_displayCacheValid = false;
    m_displayCache = QPixmap();
}

void ImageTransformer::ensureTransformCacheValid() const
{
    // Check if cache needs to be rebuilt
    if (m_cachedRotation == m_rotationAngle &&
        m_cachedFlipH == m_flipHorizontal &&
        m_cachedFlipV == m_flipVertical &&
        !m_transformedCache.isNull()) {
        return;  // Cache is valid
    }

    // Rebuild cache
    if (m_rotationAngle != 0 || m_flipHorizontal || m_flipVertical) {
        QTransform transform;

        if (m_rotationAngle != 0) {
            transform.rotate(m_rotationAngle);
        }

        if (m_flipHorizontal || m_flipVertical) {
            qreal scaleX = m_flipHorizontal ? -1.0 : 1.0;
            qreal scaleY = m_flipVertical ? -1.0 : 1.0;
            transform.scale(scaleX, scaleY);
        }

        m_transformedCache = m_originalPixmap.transformed(transform, Qt::SmoothTransformation);
        m_transformedCache.setDevicePixelRatio(m_originalPixmap.devicePixelRatio());
    } else {
        m_transformedCache = m_originalPixmap;
    }

    m_cachedRotation = m_rotationAngle;
    m_cachedFlipH = m_flipHorizontal;
    m_cachedFlipV = m_flipVertical;

    // Invalidate display cache since transformed cache changed
    m_displayCacheValid = false;
}

void ImageTransformer::rebuildDisplayPixmap() const
{
    ensureTransformCacheValid();

    if (m_displayCacheValid &&
        qFuzzyCompare(m_cachedZoom, m_zoomLevel) &&
        m_cachedSmoothing == m_smoothing) {
        return;
    }

    QSize logicalSize = m_transformedCache.size() / m_transformedCache.devicePixelRatio();
    QSize newLogicalSize = logicalSize * m_zoomLevel;
    QSize newDeviceSize = newLogicalSize * m_transformedCache.devicePixelRatio();

    Qt::TransformationMode mode = (m_smoothing && !m_fastScaling)
        ? Qt::SmoothTransformation
        : Qt::FastTransformation;

    m_displayCache = m_transformedCache.scaled(newDeviceSize,
                                                Qt::KeepAspectRatio,
                                                mode);
    m_displayCache.setDevicePixelRatio(m_transformedCache.devicePixelRatio());

    m_cachedZoom = m_zoomLevel;
    m_cachedSmoothing = m_smoothing;
    m_displayCacheValid = true;
}

QPixmap ImageTransformer::getTransformedPixmap() const
{
    if (m_rotationAngle == 0 && !m_flipHorizontal && !m_flipVertical) {
        return m_originalPixmap;
    }

    QTransform transform;

    if (m_rotationAngle != 0) {
        transform.rotate(m_rotationAngle);
    }

    if (m_flipHorizontal || m_flipVertical) {
        qreal scaleX = m_flipHorizontal ? -1.0 : 1.0;
        qreal scaleY = m_flipVertical ? -1.0 : 1.0;
        transform.scale(scaleX, scaleY);
    }

    QPixmap transformed = m_originalPixmap.transformed(transform, Qt::SmoothTransformation);
    transformed.setDevicePixelRatio(m_originalPixmap.devicePixelRatio());
    return transformed;
}

QPixmap ImageTransformer::getDisplayPixmap() const
{
    rebuildDisplayPixmap();
    return m_displayCache;
}

QSize ImageTransformer::transformedLogicalSize() const
{
    ensureTransformCacheValid();
    return m_transformedCache.size() / m_transformedCache.devicePixelRatio();
}

QSize ImageTransformer::displaySize() const
{
    return transformedLogicalSize() * m_zoomLevel;
}
