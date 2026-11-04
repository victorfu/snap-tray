#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>

// An area chosen before recording (#194). The capture stays full screen in
// edit mode: the area becomes the preview's initial crop, in video pixels.
// In quick mode the recorded region is the area itself.
namespace SnapTray::RecordingArea {

// The area kept on its screen (logical, global coordinates), at least the
// crop minimum (64 video pixels) on each side at `devicePixelRatio`. Empty
// when it misses the screen.
QRect clampToScreen(const QRect& area, const QRect& screenGeometry, qreal devicePixelRatio);

// The video-pixel crop for `area` in a recording of `recordedRegion` (both
// logical, global) encoded at `frameSize`: scaled, even-aligned and at least
// 64 pixels like any crop. Empty when it misses the recording or covers all
// of it (no crop). `area` keeps fractional logical pixels: at 150% pixel 350
// is 233.33. With the screen's `devicePixelRatio`, a frame of the region's
// size at that ratio is scaled by it, not by the frame-to-region ratio, which
// Qt's rounded logical size skews (1707 for 2560 pixels at 150%, #210).
QRect videoCrop(const QRectF& area, const QRect& recordedRegion, const QSize& frameSize,
                qreal devicePixelRatio = 0.0);

} // namespace SnapTray::RecordingArea
