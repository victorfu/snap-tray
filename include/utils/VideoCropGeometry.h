#pragma once

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>

// Geometry for cropping a recorded video in the preview window.
// All crop rects are in video pixels; view rects are in the preview item's coordinates.
namespace SnapTray::VideoCropGeometry {

constexpr int kMinCropSide = 64;

// Aspect-fit `frameSize` inside `itemSize`, centred. Matches VideoPlaybackItem letterboxing.
QRectF aspectFitRect(const QSize& frameSize, const QSizeF& itemSize);

// Clamp to the frame, floor origin and size to even pixels, enforce kMinCropSide.
// Returns an empty QRect when the frame is invalid, the rect misses the frame,
// or the result covers the whole (even-floored) frame.
QRect normalizeCropRect(const QRect& rect, const QSize& frameSize);

// The largest rect of exactly aspectWidth:aspectHeight with even width and
// height that fits inside `bounds` (clipped to the frame), centred on it.
// Ratios whose exact even step is large (native display sizes such as
// 3456x2234) keep the paired side within one pixel of the ratio instead.
// It is grown to kMinCropSide on its shorter side when the frame allows,
// staying inside the frame. A non-positive aspect falls back to
// normalizeCropRect(). Like normalizeCropRect(), a result covering the whole
// even-floored frame is returned as an empty rect (no crop).
QRect fitAspect(const QRect& bounds, const QSize& frameSize, int aspectWidth, int aspectHeight);

QRect viewToVideo(const QRectF& viewRect, const QRectF& contentRect, const QSize& frameSize);
QRectF videoToView(const QRect& videoRect, const QRectF& contentRect, const QSize& frameSize);

// The video pixel under a view point; QPoint(-1, -1) when the point is outside the content.
QPoint viewPointToVideo(const QPointF& viewPoint, const QRectF& contentRect, const QSize& frameSize);

} // namespace SnapTray::VideoCropGeometry
