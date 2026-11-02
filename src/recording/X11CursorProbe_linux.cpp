#include "recording/CursorProbe.h"

#include <QDebug>
#include <QScopeGuard>
#include <QSocketNotifier>

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>

#include <array>

namespace SnapTray {

namespace {

// X button numbers after the user's pointer mapping; 4-7 are wheel steps.
struct X11Button {
    int number;
    Qt::MouseButton button;
};

constexpr X11Button kX11Buttons[] = {
    {1, Qt::LeftButton},
    {2, Qt::MiddleButton},
    {3, Qt::RightButton},
    {8, Qt::BackButton},
    {9, Qt::ForwardButton},
};

constexpr int kMaxPointerMap = 256;

// Reads the pointer on its own X connection: XFixes for the shape (the
// display cursor, whatever application set it) and XInput2 raw button
// events. Raw events are delivered to every listener without grabbing, so
// clicks still reach their applications unchanged.
class X11CursorProbe : public CursorProbe
{
public:
    ~X11CursorProbe() override
    {
        delete m_notifier;
        if (m_display) XCloseDisplay(m_display);
    }

    bool open()
    {
        m_display = XOpenDisplay(nullptr);
        if (!m_display) return false;
        m_root = DefaultRootWindow(m_display);

        int fixesError = 0;
        if (!XFixesQueryExtension(m_display, &m_fixesEvent, &fixesError)) {
            qWarning() << "X11CursorProbe: XFixes unavailable; pointer shapes are not recorded";
        } else {
            m_shapes = true;
            XFixesSelectCursorInput(m_display, m_root, XFixesDisplayCursorNotifyMask);
            if (XFixesCursorImage* image = XFixesGetCursorImage(m_display)) {
                m_serial = image->cursor_serial;
                XFree(image);
            }
        }

        int event = 0, error = 0;
        int major = 2, minor = 0;
        if (XQueryExtension(m_display, "XInputExtension", &m_xiOpcode, &event, &error)
            && XIQueryVersion(m_display, &major, &minor) == Success) {
            std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> bits{};
            XISetMask(bits.data(), XI_RawButtonPress);
            XISetMask(bits.data(), XI_RawButtonRelease);
            XIEventMask mask;
            mask.deviceid = XIAllMasterDevices;
            mask.mask_len = int(bits.size());
            mask.mask = bits.data();
            m_clicks = XISelectEvents(m_display, m_root, &mask, 1) == Success;
        }
        if (!m_clicks) {
            qWarning() << "X11CursorProbe: XInput2 unavailable; clicks are not recorded";
        }
        XFlush(m_display);
        m_notifier = new QSocketNotifier(ConnectionNumber(m_display), QSocketNotifier::Read);
        QObject::connect(m_notifier, &QSocketNotifier::activated, this, [this] { drainEvents(); });
        return true;
    }

    Capabilities capabilities() const override { return {m_shapes, m_clicks}; }
    QString name() const override { return QStringLiteral("x11"); }

    std::optional<State> poll() override
    {
        drainEvents();
        Window root = 0, child = 0;
        int rootX = 0, rootY = 0, winX = 0, winY = 0;
        unsigned int mask = 0;
        if (!XQueryPointer(m_display, m_root, &root, &child, &rootX, &rootY, &winX, &winY, &mask)) {
            return std::nullopt; // on another X screen
        }
        return State{QPoint(rootX, rootY), true, m_serial};
    }

    std::optional<Shape> currentShape() override
    {
        if (!m_shapes) return std::nullopt;
        XFixesCursorImage* cursor = XFixesGetCursorImage(m_display);
        if (!cursor) return std::nullopt;
        Shape shape;
        shape.hotspot = QPoint(cursor->xhot, cursor->yhot);
        shape.image = QImage(cursor->width, cursor->height, QImage::Format_ARGB32_Premultiplied);
        // XFixes pixels are premultiplied ARGB in the low 32 bits of each long.
        for (int y = 0; y < cursor->height; ++y) {
            auto* row = reinterpret_cast<QRgb*>(shape.image.scanLine(y));
            for (int x = 0; x < cursor->width; ++x) {
                row[x] = QRgb(cursor->pixels[y * cursor->width + x] & 0xffffffffUL);
            }
        }
        m_serial = cursor->cursor_serial;
        XFree(cursor);
        return shape;
    }

private:
    void loadPointerMapping()
    {
        m_pointerMap.fill(0);
        unsigned char map[kMaxPointerMap];
        const int count = XGetPointerMapping(m_display, map, kMaxPointerMap);
        for (int i = 0; i < count; ++i) m_pointerMap[i + 1] = map[i];
    }

    void drainEvents()
    {
        // A button signal's handler polls the position, which drains again:
        // a nested drain would deliver a queued release inside the press
        // handler, before the press is recorded (#174 review).
        if (m_draining) return;
        m_draining = true;
        const auto done = qScopeGuard([this] { m_draining = false; });
        while (XPending(m_display) > 0) {
            XEvent event;
            XNextEvent(m_display, &event);
            if (m_shapes && event.type == m_fixesEvent + XFixesCursorNotify) {
                m_serial = reinterpret_cast<XFixesCursorNotifyEvent*>(&event)->cursor_serial;
                continue;
            }
            XGenericEventCookie* cookie = &event.xcookie;
            if (event.type != GenericEvent || cookie->extension != m_xiOpcode
                || !XGetEventData(m_display, cookie)) {
                continue;
            }
            if (cookie->evtype == XI_RawButtonPress || cookie->evtype == XI_RawButtonRelease) {
                const auto* raw = static_cast<const XIRawEvent*>(cookie->data);
                // Raw events carry the physical button; apply the user's
                // mapping, re-read per click so a left-handed switch made
                // while recording applies (no MappingNotify is guaranteed).
                loadPointerMapping();
                const int physical = raw->detail;
                const int logical = physical > 0 && physical < kMaxPointerMap ? m_pointerMap[physical] : 0;
                for (const auto& entry : kX11Buttons) {
                    if (entry.number == logical) {
                        emit buttonChanged(entry.button, cookie->evtype == XI_RawButtonPress);
                        break;
                    }
                }
            }
            XFreeEventData(m_display, cookie);
        }
    }

    Display* m_display = nullptr;
    Window m_root = 0;
    QSocketNotifier* m_notifier = nullptr;
    int m_fixesEvent = 0;
    int m_xiOpcode = 0;
    bool m_shapes = false;
    bool m_clicks = false;
    quint64 m_serial = 0;
    bool m_draining = false;
    std::array<int, kMaxPointerMap> m_pointerMap{};
};

} // namespace

std::unique_ptr<CursorProbe> createX11CursorProbe()
{
    auto probe = std::make_unique<X11CursorProbe>();
    if (!probe->open()) {
        qWarning() << "X11CursorProbe: cannot open the X display; recording pointer positions only";
        return nullptr;
    }
    return probe;
}

} // namespace SnapTray
