#pragma once

#include <QRect>
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
// of it (no crop).
QRect videoCrop(const QRect& area, const QRect& recordedRegion, const QSize& frameSize);

} // namespace SnapTray::RecordingArea
