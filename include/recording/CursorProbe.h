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
        bool clicks = false; // button presses outside SnapTray's own windows
    };

    struct State {
        QPoint position;
        bool physical = false;   // native desktop pixels; otherwise Qt logical coordinates
        quint64 shapeSerial = 0; // changes whenever the shape changes; 0 = unknown
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
    // The current shape image; nullopt without shape support.
    virtual std::optional<Shape> currentShape() { return std::nullopt; }
    // Short name for diagnostics ("x11", "position-only").
    virtual QString name() const = 0;

signals:
    // A physical button changed, already mapped to the user's button order
    // (left-handed setups report their primary button as Left). ageMs is
    // how long ago it happened, for events delivered late.
    void buttonChanged(Qt::MouseButton button, bool down, qint64 ageMs = 0);
};

// The best probe this platform and session supports. Never null: without a
// native source it falls back to position-only polling.
std::unique_ptr<CursorProbe> createCursorProbe();

} // namespace SnapTray
