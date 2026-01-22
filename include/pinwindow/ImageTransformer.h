#ifndef IMAGETRANSFORMER_H
#define IMAGETRANSFORMER_H

#include <QObject>
#include <QPixmap>
#include <QSize>

class ImageTransformer : public QObject
{
    Q_OBJECT

public:
    explicit ImageTransformer(QObject* parent = nullptr);
    ~ImageTransformer() override = default;

    QPixmap originalPixmap() const { return m_originalPixmap; }

    // Rotation operations (90 degree increments)
    void rotateRight();   // Rotate clockwise by 90 degrees
    void rotateLeft();    // Rotate counter-clockwise by 90 degrees
    int rotationAngle() const { return m_rotationAngle; }

    // Flip operations
    void flipHorizontal();
    void flipVertical();
    bool isFlippedHorizontal() const { return m_flipHorizontal; }
    bool isFlippedVertical() const { return m_flipVertical; }

    // Zoom operations
    void setZoomLevel(qreal zoom);
    qreal zoomLevel() const { return m_zoomLevel; }

    bool smoothing() const { return m_smoothing; }

    // Get the fully transformed pixmap (rotation + flip + zoom)
    QPixmap getTransformedPixmap() const;

    // Get the display pixmap at current settings
    QPixmap getDisplayPixmap() const;

    // Get the logical size after transformation (before zoom)
    QSize transformedLogicalSize() const;

    // Get the display size (after zoom)
    QSize displaySize() const;

    // Invalidate the cache to force rebuild
    void invalidateCache();

signals:
    void transformChanged();

private:
    void ensureTransformCacheValid() const;
    void rebuildDisplayPixmap() const;

    QPixmap m_originalPixmap;

    // Transform state
    int m_rotationAngle = 0;      // 0, 90, 180, 270
    bool m_flipHorizontal = false;
    bool m_flipVertical = false;
    qreal m_zoomLevel = 1.0;
    bool m_smoothing = true;
    bool m_fastScaling = false;

    // Cache for rotated/flipped pixmap (before zoom)
    mutable QPixmap m_transformedCache;
    mutable int m_cachedRotation = -1;
    mutable bool m_cachedFlipH = false;
    mutable bool m_cachedFlipV = false;

    // Cache for display pixmap (after zoom)
    mutable QPixmap m_displayCache;
    mutable qreal m_cachedZoom = -1.0;
    mutable bool m_cachedSmoothing = true;
    mutable bool m_displayCacheValid = false;
};

#endif // IMAGETRANSFORMER_H
