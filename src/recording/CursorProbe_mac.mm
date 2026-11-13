#include "recording/CursorProbe_mac.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <mach/mach_time.h>

namespace SnapTray {

namespace MacCursor {

namespace {

struct NumberedButton {
    qint64 number;
    Qt::MouseButton button;
};

constexpr NumberedButton kButtons[] = {
    {0, Qt::LeftButton},
    {1, Qt::RightButton},
    {2, Qt::MiddleButton},
    {3, Qt::BackButton},
    {4, Qt::ForwardButton},
};

struct ButtonEventType {
    int type;
    bool down;
};

constexpr ButtonEventType kButtonEventTypes[] = {
    {kCGEventLeftMouseDown, true},  {kCGEventLeftMouseUp, false},
    {kCGEventRightMouseDown, true}, {kCGEventRightMouseUp, false},
    {kCGEventOtherMouseDown, true}, {kCGEventOtherMouseUp, false},
};

constexpr qint64 kNanosecondsPerMillisecond = 1000000;
// Far beyond any real pointer (the sidecar itself stores at most 256 px):
// only keeps a broken image from allocating without bound.
constexpr int kMaxShapeEdgePx = 1024;

} // namespace

std::optional<Qt::MouseButton> buttonForNumber(qint64 number)
{
    for (const auto& entry : kButtons) {
        if (entry.number == number) return entry.button;
    }
    return std::nullopt;
}

std::optional<bool> pressForEventType(int eventType)
{
    for (const auto& entry : kButtonEventTypes) {
        if (entry.type == eventType) return entry.down;
    }
    return std::nullopt;
}

namespace {

// ticks * numer / denom without overflowing 64 bits.
quint64 scaled(quint64 ticks, quint32 numer, quint32 denom)
{
    if (denom == 0) return ticks;
    return (ticks / denom) * numer + (ticks % denom) * numer / denom;
}

} // namespace

quint64 UptimeClock::nanoseconds() const
{
    return scaled(ticks, numer, denom);
}

qint64 eventAgeMs(const UptimeClock& now, quint64 eventStamp)
{
    // Not ahead of the tick clock: a tick stamp (on Intel the two are one).
    if (eventStamp <= now.ticks) {
        return qint64(scaled(now.ticks - eventStamp, now.numer, now.denom) / kNanosecondsPerMillisecond);
    }
    const quint64 nowNs = now.nanoseconds();
    return nowNs > eventStamp ? qint64((nowNs - eventStamp) / kNanosecondsPerMillisecond) : 0;
}

std::optional<CursorProbe::Shape> shapeFromImage(NSImage* image, CGPoint hotSpotPoints, qreal scale)
{
    if (!image || scale <= 0.0) return std::nullopt;
    const NSSize points = image.size;
    const int width = qRound(points.width * scale);
    const int height = qRound(points.height * scale);
    if (width <= 0 || height <= 0 || width > kMaxShapeEdgePx || height > kMaxShapeEdgePx) {
        return std::nullopt;
    }

    CursorProbe::Shape shape;
    shape.image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
    if (shape.image.isNull()) return std::nullopt;
    shape.image.fill(Qt::transparent);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    // QImage's premultiplied ARGB32 is a host-endian 0xAARRGGBB word. The
    // first row of a bitmap context is the top of the picture, as in QImage.
    CGContextRef context = CGBitmapContextCreate(
        shape.image.bits(), size_t(width), size_t(height), 8, size_t(shape.image.bytesPerLine()), colorSpace,
        uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Host));
    CGColorSpaceRelease(colorSpace);
    if (!context) return std::nullopt;

    // Drawing into the pixel-sized rectangle lets AppKit pick the image's
    // representation for this scale (1x or 2x) instead of resampling one.
    [NSGraphicsContext saveGraphicsState];
    NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
    [image drawInRect:NSMakeRect(0, 0, width, height)
             fromRect:NSZeroRect
            operation:NSCompositingOperationCopy
             fraction:1.0];
    [NSGraphicsContext restoreGraphicsState];
    CGContextRelease(context);

    shape.hotspot = QPoint(qRound(hotSpotPoints.x * scale), qRound(hotSpotPoints.y * scale));
    return shape;
}

} // namespace MacCursor

namespace {

// The clock event timestamps count on (time awake since startup).
MacCursor::UptimeClock uptimeNow()
{
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return info;
    }();
    return {mach_absolute_time(), timebase.numer, timebase.denom};
}

bool pointerVisible()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return CGCursorIsVisible();
#pragma clang diagnostic pop
}

// Pixels per point of the screen the pointer is on. The pointer is recorded
// in the pixels of the screen it is drawn on, which is the recorded screen
// whenever the pointer is inside the video.
qreal scaleUnderPointer()
{
    const NSPoint location = NSEvent.mouseLocation;
    for (NSScreen* screen in NSScreen.screens) {
        if (NSMouseInRect(location, screen.frame, NO)) return screen.backingScaleFactor;
    }
    NSScreen* main = NSScreen.mainScreen;
    return main ? main.backingScaleFactor : 1.0;
}

// What a hidden pointer is reported as: the recorder reads a fully
// transparent image as "not visible".
CursorProbe::Shape hiddenShape()
{
    QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return {image, QPoint()};
}

bool sameShape(const std::optional<CursorProbe::Shape>& a, const std::optional<CursorProbe::Shape>& b)
{
    if (!a || !b) return !a && !b;
    return a->hotspot == b->hotspot && a->image == b->image;
}

constexpr CGEventMask kButtonEventMask =
    CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp)
    | CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventRightMouseUp)
    | CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp);

// The system pointer, read on the GUI thread: its image from
// NSCursor.currentSystemCursor, its visibility from CGCursorIsVisible, and
// button presses from a listener that never consumes, changes or delays an
// event. How well the first two follow a pointer that another application
// sets or hides is on the device checklist
// (docs/developer/recording-sync-matrix.md), not established here.
//
// Presses come from one of two sources. Neither asks for a permission:
//  - a listen-only CGEventTap, used only when listening is already allowed
//    (CGPreflightListenEventAccess, i.e. Input Monitoring was granted
//    earlier, for example for shortcut hints). Creating a tap without that
//    could show the system prompt, which only a setting the user switched on
//    may do, so it is not attempted.
//  - otherwise an NSEvent global monitor, which needs no permission for
//    mouse events (only key events are restricted).
//
// The two differ in one way: the monitor does not see presses on SnapTray's
// own windows and the tap does (as the X11 and Windows probes do). Leaving
// those out everywhere is #323.
//
// Both are expected to report the buttons after the system's "primary mouse
// button" setting (the swap is applied below the session event stream), so
// the primary button arrives as the left one. That is not documented and is
// on the device checklist.
class MacCursorProbe final : public CursorProbe
{
public:
    MacCursorProbe()
    {
        // The run-loop source, the monitor and NSCursor all belong to the main thread.
        Q_ASSERT(QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread());
        if (!CGPreflightListenEventAccess() || !startTap()) startMonitor();
        refreshShape();
        m_shapeClock.start();
    }

    ~MacCursorProbe() override
    {
        if (m_monitor) [NSEvent removeMonitor:m_monitor];
        stopTap();
    }

    Capabilities capabilities() const override { return {true, m_tap != nullptr || m_monitor != nil}; }

    QString name() const override
    {
        if (m_tap) return QStringLiteral("macos-event-tap");
        return m_monitor ? QStringLiteral("macos-event-monitor") : QStringLiteral("macos-shapes-only");
    }

    std::optional<State> poll() override
    {
        if (m_shapeClock.elapsed() >= MacCursor::kShapePollIntervalMs) {
            refreshShape();
            m_shapeClock.restart();
        }
        return State{QCursor::pos(), false, m_serial};
    }

    std::optional<Shape> currentShape() override { return m_shape; }

private:
    // False when the tap cannot be used; nothing is left behind then.
    bool startTap()
    {
        m_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
                                 kButtonEventMask, tapCallback, this);
        if (!m_tap) {
            qWarning() << "MacCursorProbe: the event tap was refused; using the event monitor for clicks";
            return false;
        }
        m_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_tap, 0);
        if (!m_source) {
            qWarning() << "MacCursorProbe: no run-loop source for the event tap; using the event monitor for clicks";
            stopTap();
            return false;
        }
        CFRunLoopAddSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes);
        CGEventTapEnable(m_tap, true);
        if (!CGEventTapIsEnabled(m_tap)) {
            qWarning() << "MacCursorProbe: the event tap stays disabled; using the event monitor for clicks";
            stopTap();
            return false;
        }
        return true;
    }

    void stopTap()
    {
        if (m_source) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes);
            CFRelease(m_source);
            m_source = nullptr;
        }
        if (m_tap) {
            CGEventTapEnable(m_tap, false);
            CFMachPortInvalidate(m_tap);
            CFRelease(m_tap);
            m_tap = nullptr;
        }
    }

    void startMonitor()
    {
        const NSEventMask mask = NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskRightMouseDown
                                 | NSEventMaskRightMouseUp | NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp;
        // Removed in the destructor before `this` goes away; delivered on the main thread.
        MacCursorProbe* self = this;
        m_monitor = [NSEvent addGlobalMonitorForEventsMatchingMask:mask
                                                           handler:^(NSEvent* event) {
                                                               self->report(int(event.type), event.buttonNumber,
                                                                            quint64(event.timestamp * NSEC_PER_SEC));
                                                           }];
        if (!m_monitor) qWarning() << "MacCursorProbe: no source for button events; clicks are not recorded";
    }

    static CGEventRef tapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void* context)
    {
        auto* self = static_cast<MacCursorProbe*>(context);
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            self->tapWasDisabled();
            return event;
        }
        self->report(int(type), CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber),
                     CGEventGetTimestamp(event));
        return event; // Listen-only: the event goes on unchanged.
    }

    // The system switches a tap off when its callback was slow or on some
    // user input; it has to be switched back on. Button events from the gap
    // are gone, which the recorder is told.
    void tapWasDisabled()
    {
        if (!m_tap) return;
        ++m_tapDisabledCount;
        qWarning() << "MacCursorProbe: the system disabled the event tap (" << m_tapDisabledCount
                   << "times so far); button events may have been missed";
        emit buttonEventsLost();
        CGEventTapEnable(m_tap, true);
        if (CGEventTapIsEnabled(m_tap)) return;
        // It will not come back (for example Input Monitoring was revoked):
        // use the monitor instead. Not from inside the tap's own callback.
        qWarning() << "MacCursorProbe: the event tap cannot be re-enabled; switching to the event monitor";
        QMetaObject::invokeMethod(this, [this] {
            if (!m_tap) return;
            stopTap();
            if (!m_monitor) startMonitor();
        }, Qt::QueuedConnection);
    }

    void report(int eventType, qint64 buttonNumber, quint64 eventStamp)
    {
        const auto down = MacCursor::pressForEventType(eventType);
        const auto button = MacCursor::buttonForNumber(buttonNumber);
        if (!down || !button) return;
        emit buttonChanged(*button, *down, MacCursor::eventAgeMs(uptimeNow(), eventStamp));
    }

    // Re-reads the pointer. The converted pixels themselves are compared:
    // that is exact, and no dearer than encoding the image to digest it.
    void refreshShape()
    {
        std::optional<Shape> next;
        if (!pointerVisible()) {
            next = hiddenShape();
        } else {
            @autoreleasepool {
                // nil when the system cannot say: recorded as an unknown shape.
                NSCursor* cursor = NSCursor.currentSystemCursor;
                if (cursor) {
                    next = MacCursor::shapeFromImage(cursor.image, NSPointToCGPoint(cursor.hotSpot),
                                                     scaleUnderPointer());
                }
                if (!next && !m_unreadableReported) {
                    m_unreadableReported = true;
                    qWarning() << "MacCursorProbe: a pointer shape is recorded as unknown:"
                               << (cursor ? "its image cannot be converted" : "the system reports no current cursor");
                }
            }
        }
        if (sameShape(next, m_shape)) return;
        m_shape = std::move(next);
        ++m_serial;
    }

    CFMachPortRef m_tap = nullptr;
    CFRunLoopSourceRef m_source = nullptr;
    id m_monitor = nil;
    std::optional<Shape> m_shape;
    // Never 0, which means "unknown" to the recorder; grows with every change.
    quint64 m_serial = 1;
    int m_tapDisabledCount = 0;
    bool m_unreadableReported = false; // logged once: it repeats on every refresh
    QElapsedTimer m_shapeClock;
};

} // namespace

std::unique_ptr<CursorProbe> createMacCursorProbe()
{
    return std::make_unique<MacCursorProbe>();
}

} // namespace SnapTray
