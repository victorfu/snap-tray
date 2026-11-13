#pragma once

#include "recording/CursorProbe.h"

#include <optional>

#ifdef __OBJC__
#import <CoreGraphics/CoreGraphics.h>
@class NSImage;
#endif

// The pieces of the macOS cursor probe (#322) that work without a live
// pointer, declared here so they can be tested. Built on macOS only.
namespace SnapTray::MacCursor {

// How often the pointer's shape and visibility are re-read. Positions are
// polled faster than this by the recorder; a shape does not need to be.
inline constexpr int kShapePollIntervalMs = 33;

// A mouse button number as CGEvent (kCGMouseEventButtonNumber) and NSEvent
// (buttonNumber) report it. nullopt for buttons that are not recorded.
std::optional<Qt::MouseButton> buttonForNumber(qint64 number);

// Whether a mouse event type is a press (true) or a release (false). The
// CGEventType and NSEventType values of the six button events are the same.
// nullopt for any other event.
std::optional<bool> pressForEventType(int eventType);

// How long ago an event happened, from two readings of the uptime clock in
// nanoseconds. Never negative.
qint64 eventAgeMs(quint64 nowNs, quint64 eventNs);

#ifdef __OBJC__
// A cursor image as recorded: premultiplied ARGB at `scale` pixels per
// point, with the hotspot (points from the top-left) in the same pixels.
// nullopt when there is no usable image.
std::optional<CursorProbe::Shape> shapeFromImage(NSImage* image, CGPoint hotSpotPoints, qreal scale);
#endif

} // namespace SnapTray::MacCursor
