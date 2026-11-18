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
 * Positions move one source pixel at a time, unless setPositionStep() asks
 * for a coarser grid. Extents are the nearest whole number of logical pixels
 * at the image's device pixel ratio, or the full image extent. At an integer
 * ratio (1x, 2x, 3x) that is a size the pin shows without resampling: any
 * size at 1x, even sizes at 2x, where a 101 px crop would otherwise be
 * stretched to 102 px on screen and in exports. At a fractional ratio it is
 * only the closest fit: 101 px at 1.5x is 67.33 logical pixels and is still
 * shown at 67, as any capture of that size is.
 *
 * Every rect the model holds was produced by its own rules. A pointer drag is
 * a gesture: beginGesture() remembers the draft, the drag functions work from
 * that snapshot, and cancelGesture() puts it back.
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

    // Grid the draft's top-left corner stays on, in source pixels per axis.
    // The host raises it above 1 when annotations must move with the crop by
    // a whole number of their own units. Re-snaps the current draft.
    void setPositionStep(const QSize& step);
    QSize positionStep() const { return m_positionStep; }
    // Smallest step whose multiples are whole view units, given how many view
    // units one source pixel covers; 0 when none up to kMaxPositionStep is.
    static int exactPositionStep(qreal viewPerPixel);
    static constexpr int kMaxPositionStep = 64;

    bool hasRect() const { return !m_rect.isEmpty(); }
    QRect rect() const { return m_rect; }
    void clear();

    Aspect aspect() const { return m_aspect; }
    // Selects a preset and re-fits the current draft around its center.
    // Returns false, keeping the old preset and draft, when the draft cannot
    // take that ratio (degenerate image sizes).
    bool setAspect(Aspect aspect);

    // A new selection from a fixed corner to the pointer.
    void setRectFromDrag(const QPoint& anchor, const QPoint& current);
    // Moves the draft by position steps, kept inside the image.
    void nudge(const QPoint& steps);

    // ---- pointer gesture ----
    // Remembers the draft as it is now. The functions below work from it.
    void beginGesture();
    bool gestureActive() const { return m_gestureActive; }
    // The remembered draft, moved by `delta` source pixels.
    void moveBy(const QPoint& delta);
    // One handle of the remembered draft dragged to `current`.
    void dragHandle(Handle handle, const QPoint& current);
    // Ends the gesture. A drag that left nothing selectable keeps the
    // remembered draft instead.
    void endGesture();
    // Ends the gesture and puts the remembered draft back.
    void cancelGesture();

    // The size nearest to `pixels`, no larger than `maxPixels`, that the pin
    // shows without resampling; 0 when none fits. The whole image extent is
    // always allowed.
    int legalExtent(int pixels, int maxPixels, Qt::Orientation axis) const;

private:
    // Width / height in source pixels, 0 when unconstrained.
    qreal sourceRatio() const;
    QSize fitSize(qreal width, qreal height, int maxWidth, int maxHeight) const;
    void setRectClamped(const QRect& rect);
    void moveRect(const QRect& startRect, const QPoint& delta);
    void resizeRect(const QRect& startRect, Handle handle, const QPoint& current);
    // Puts the corner of `rect` on the position grid, inside the image.
    QRect snapped(const QRect& rect) const;

    QSize m_sourceSize;
    qreal m_dpr = 1.0;
    bool m_viewTransposed = false;
    Aspect m_aspect = Aspect::Free;
    QRect m_rect;
    QSize m_positionStep{1, 1};
    bool m_gestureActive = false;
    QRect m_gestureStartRect;
};

#endif // CROPDRAFTMODEL_H
