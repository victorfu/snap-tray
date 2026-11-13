#pragma once

#include <QImage>
#include <QObject>
#include <QPoint>

#include <memory>
#include <optional>

namespace SnapTray {

// Where the pointer is and what it looks like, read passively: no input is
// grabbed, intercepted or delayed, and no keyboard state is read.
class CursorProbe : public QObject
{
    Q_OBJECT

public:
    struct Capabilities {
        bool shapes = false; // real pointer images and hotspots
        // Button presses anywhere on the desktop. Whether presses on
        // SnapTray's own windows are among them depends on the source.
        bool clicks = false;
    };

    struct State {
        QPoint position;
        bool physical = false;   // native desktop pixels; otherwise Qt logical coordinates
        // Changes whenever the shape changes; 0 = unknown. A serial says
        // "ask again", not that the shape can be read.
        quint64 shapeSerial = 0;
    };

    struct Shape {
        QImage image; // premultiplied ARGB, native pixels
        QPoint hotspot;
    };

    using QObject::QObject;
    ~CursorProbe() override = default;

    virtual Capabilities capabilities() const = 0;
    // nullopt when the pointer cannot be read right now.
    virtual std::optional<State> poll() = 0;
    // The shape poll() last saw. nullopt without shape support, before the
    // first poll(), or when this shape cannot be read (recorded as unknown);
    // a fully transparent image for a hidden pointer.
    virtual std::optional<Shape> currentShape() { return std::nullopt; }
    // Short name for diagnostics ("x11", "windows", "macos-event-tap",
    // "macos-event-monitor", "macos-shapes-only", "position-only").
    virtual QString name() const = 0;

signals:
    // A physical button changed, already mapped to the user's button order
    // (left-handed setups report their primary button as Left). ageMs is
    // how long ago it happened, for events delivered late.
    void buttonChanged(Qt::MouseButton button, bool down, qint64 ageMs = 0);
    // Some button events did not make it (a full queue, an event source the
    // system switched off for a while). Which ones is not known, so a button
    // may look held when it is not.
    void buttonEventsLost();
};

// The best probe this platform and session supports. Never null: without a
// native source it falls back to position-only polling.
std::unique_ptr<CursorProbe> createCursorProbe();

#ifdef Q_OS_WIN
// CursorProbe_win.cpp. Shapes always; clicks unless the mouse hook is refused.
std::unique_ptr<CursorProbe> createWindowsCursorProbe();
// The picture and hotspot of a cursor handle (an HCURSOR). nullopt when it
// cannot be read; a fully transparent picture when it draws nothing.
std::optional<CursorProbe::Shape> windowsCursorShape(void* cursorHandle);
#endif

} // namespace SnapTray
