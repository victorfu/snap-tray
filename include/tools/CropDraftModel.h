#ifndef CROPDRAFTMODEL_H
#define CROPDRAFTMODEL_H

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <Qt>

/**
 * @brief Uncommitted crop selection of a pinned image (#358).
 *
 * Everything here is in source pixels of the image being cropped, not in the
 * zoomed display or in logical (device-independent) units. Points are grid
 * lines: x runs from 0 to the image width inclusive, so a rect spanning
 * [0, width] covers every column.
 *
 * Positions move one source pixel at a time. Extents are kept to sizes the pin
 * shows without resampling: a whole number of logical pixels at the image's
 * device pixel ratio (any size at 1x, even sizes at 2x), or the full image
 * extent. Without that rule a 101 px crop of a 2x image would be stretched to
 * 102 px on screen and in exports.
 */
class CropDraftModel
{
public:
    enum class Aspect {
        Free = 0,
        Original,
        Square,
        Wide16x9,
        Tall9x16,
        Standard4x3,
        Classic3x2,
    };

    enum class Handle {
        None = 0,
        Move,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    struct AspectOption {
        Aspect aspect;
        int widthUnits;   // 0 for Free and Original
        int heightUnits;
    };

    // Every preset, in the order the UI lists them.
    static const QVector<AspectOption>& aspectOptions();
    static bool isValidAspect(int value);

    // Sets the image the draft belongs to. A different size discards the draft.
    void setSource(const QSize& pixelSize, qreal devicePixelRatio);
    QSize sourceSize() const { return m_sourceSize; }

    // The view shows the image rotated by 90 or 270 degrees, so a preset such
    // as 16:9 is 9:16 in source pixels. Re-applies the preset to the draft.
    void setViewTransposed(bool transposed);
    bool viewTransposed() const { return m_viewTransposed; }

    bool hasRect() const { return !m_rect.isEmpty(); }
    QRect rect() const { return m_rect; }
    void clear() { m_rect = QRect(); }
    // Restores a rect produced earlier by this model (for cancelling a drag).
    void restoreRect(const QRect& rect);

    Aspect aspect() const { return m_aspect; }
    // Re-fits the current draft around its center.
    void setAspect(Aspect aspect);

    // A new selection from a fixed corner to the pointer.
    void setRectFromDrag(const QPoint& anchor, const QPoint& current);
    // Moves the draft `startRect` by `delta`, kept inside the image.
    void moveRect(const QRect& startRect, const QPoint& delta);
    void nudge(const QPoint& delta);
    // Drags one handle of `startRect` to `current`.
    void resizeRect(const QRect& startRect, Handle handle, const QPoint& current);

    // The size nearest to `pixels`, no larger than `maxPixels`, that the pin
    // shows without resampling; 0 when none fits. The whole image extent is
    // always allowed.
    int legalExtent(int pixels, int maxPixels, Qt::Orientation axis) const;

private:
    // Width / height in source pixels, 0 when unconstrained.
    qreal sourceRatio() const;
    QSize fitSize(qreal width, qreal height, int maxWidth, int maxHeight) const;
    void setRectClamped(const QRect& rect);

    QSize m_sourceSize;
    qreal m_dpr = 1.0;
    bool m_viewTransposed = false;
    Aspect m_aspect = Aspect::Free;
    QRect m_rect;
};

#endif // CROPDRAFTMODEL_H
