#include "recording/KeyProbe.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSocketNotifier>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>

#include <array>

namespace SnapTray {

namespace {

struct KeysymKey {
    KeySym keysym;
    int key;
};

// Named keys; letters, digits and F-keys are mapped by range.
constexpr KeysymKey kNamedKeysyms[] = {
    {XK_Escape, Qt::Key_Escape}, {XK_Tab, Qt::Key_Tab}, {XK_ISO_Left_Tab, Qt::Key_Backtab},
    {XK_BackSpace, Qt::Key_Backspace}, {XK_Return, Qt::Key_Return}, {XK_KP_Enter, Qt::Key_Enter},
    {XK_Insert, Qt::Key_Insert}, {XK_Delete, Qt::Key_Delete}, {XK_Pause, Qt::Key_Pause},
    {XK_Print, Qt::Key_Print}, {XK_Home, Qt::Key_Home}, {XK_End, Qt::Key_End}, {XK_Left, Qt::Key_Left},
    {XK_Up, Qt::Key_Up}, {XK_Right, Qt::Key_Right}, {XK_Down, Qt::Key_Down}, {XK_Page_Up, Qt::Key_PageUp},
    {XK_Page_Down, Qt::Key_PageDown}, {XK_Menu, Qt::Key_Menu}, {XK_space, Qt::Key_Space},
    {XK_Shift_L, Qt::Key_Shift}, {XK_Shift_R, Qt::Key_Shift}, {XK_Control_L, Qt::Key_Control},
    {XK_Control_R, Qt::Key_Control}, {XK_Alt_L, Qt::Key_Alt}, {XK_Alt_R, Qt::Key_Alt},
    {XK_Meta_L, Qt::Key_Meta}, {XK_Meta_R, Qt::Key_Meta}, {XK_Super_L, Qt::Key_Super_L},
    {XK_Super_R, Qt::Key_Super_R}, {XK_ISO_Level3_Shift, Qt::Key_AltGr}, {XK_Caps_Lock, Qt::Key_CapsLock},
    {XK_Num_Lock, Qt::Key_NumLock}, {XK_Scroll_Lock, Qt::Key_ScrollLock},
    {XK_minus, Qt::Key_Minus}, {XK_equal, Qt::Key_Equal}, {XK_bracketleft, Qt::Key_BracketLeft},
    {XK_bracketright, Qt::Key_BracketRight}, {XK_semicolon, Qt::Key_Semicolon},
    {XK_apostrophe, Qt::Key_Apostrophe}, {XK_grave, Qt::Key_QuoteLeft}, {XK_backslash, Qt::Key_Backslash},
    {XK_comma, Qt::Key_Comma}, {XK_period, Qt::Key_Period}, {XK_slash, Qt::Key_Slash},
};

int qtKeyFor(KeySym keysym)
{
    if (keysym >= XK_a && keysym <= XK_z) return Qt::Key_A + int(keysym - XK_a);
    if (keysym >= XK_A && keysym <= XK_Z) return Qt::Key_A + int(keysym - XK_A);
    if (keysym >= XK_0 && keysym <= XK_9) return Qt::Key_0 + int(keysym - XK_0);
    if (keysym >= XK_F1 && keysym <= XK_F35) return Qt::Key_F1 + int(keysym - XK_F1);
    if (keysym >= XK_KP_0 && keysym <= XK_KP_9) return Qt::Key_0 + int(keysym - XK_KP_0);
    for (const auto& entry : kNamedKeysyms) {
        if (entry.keysym == keysym) return entry.key;
    }
    return 0;
}

Qt::KeyboardModifier modifierFor(int key)
{
    switch (key) {
    case Qt::Key_Shift: return Qt::ShiftModifier;
    case Qt::Key_Control: return Qt::ControlModifier;
    case Qt::Key_Alt: return Qt::AltModifier;
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R: return Qt::MetaModifier;
    default: return Qt::NoModifier;
    }
}

// What a keysym types, for the all-keys mode: Latin-1 and Unicode keysyms.
QString textFor(KeySym keysym)
{
    if (keysym >= 0x20 && keysym <= 0xff) return QString(QChar(char16_t(keysym)));
    if ((keysym & 0xff000000) == 0x01000000) {
        const char32_t codepoint = char32_t(keysym & 0x00ffffff);
        return QString::fromUcs4(&codepoint, 1);
    }
    return QString();
}

// Reads key presses on its own X connection through XInput2 raw events,
// which every listener receives without a grab: keys still reach their
// applications unchanged and nothing is delayed. X11 has no secure-input
// signal, so the default shortcuts-only mode is what keeps typing out.
class X11KeyProbe : public KeyProbe
{
public:
    ~X11KeyProbe() override
    {
        delete m_notifier;
        if (m_display) XCloseDisplay(m_display);
    }

    bool open()
    {
        m_display = XOpenDisplay(nullptr);
        if (!m_display) return false;
        int event = 0, error = 0, major = 2, minor = 2;
        if (!XQueryExtension(m_display, "XInputExtension", &m_xiOpcode, &event, &error)
            || XIQueryVersion(m_display, &major, &minor) != Success || major < 2) {
            qWarning() << "X11KeyProbe: XInput2 unavailable; keys are not recorded";
            return false;
        }
        std::array<unsigned char, XIMaskLen(XI_LASTEVENT)> bits{};
        XISetMask(bits.data(), XI_RawKeyPress);
        XISetMask(bits.data(), XI_RawKeyRelease);
        XIEventMask mask;
        mask.deviceid = XIAllMasterDevices;
        mask.mask_len = int(bits.size());
        mask.mask = bits.data();
        if (XISelectEvents(m_display, DefaultRootWindow(m_display), &mask, 1) != Success) return false;
        XFlush(m_display);
        m_notifier = new QSocketNotifier(ConnectionNumber(m_display), QSocketNotifier::Read);
        QObject::connect(m_notifier, &QSocketNotifier::activated, this, [this] { drainEvents(); });
        return true;
    }

    QString name() const override { return QStringLiteral("x11"); }

private:
    void drainEvents()
    {
        while (XPending(m_display) > 0) {
            XEvent event;
            XNextEvent(m_display, &event);
            XGenericEventCookie* cookie = &event.xcookie;
            if (event.type != GenericEvent || cookie->extension != m_xiOpcode || !XGetEventData(m_display, cookie)) {
                continue;
            }
            const auto done = qScopeGuard([this, cookie] { XFreeEventData(m_display, cookie); });
            if (cookie->evtype != XI_RawKeyPress && cookie->evtype != XI_RawKeyRelease) continue;
            const auto* raw = static_cast<const XIRawEvent*>(cookie->data);
            const bool down = cookie->evtype == XI_RawKeyPress;
            const KeySym base = XkbKeycodeToKeysym(m_display, KeyCode(raw->detail), 0, 0);
            const int key = qtKeyFor(base);
            if (const Qt::KeyboardModifier modifier = modifierFor(key)) {
                m_modifiers.setFlag(modifier, down);
                continue;
            }
            if (!down || (raw->flags & XIKeyRepeat) || key == 0) continue;
            const KeySym typed = XkbKeycodeToKeysym(m_display, KeyCode(raw->detail), 0,
                                                    m_modifiers.testFlag(Qt::ShiftModifier) ? 1 : 0);
            emit keyPressed(key, m_modifiers, textFor(typed), 0);
        }
    }

    Display* m_display = nullptr;
    QSocketNotifier* m_notifier = nullptr;
    int m_xiOpcode = 0;
    Qt::KeyboardModifiers m_modifiers;
};

} // namespace

std::unique_ptr<KeyProbe> createX11KeyProbe()
{
    auto probe = std::make_unique<X11KeyProbe>();
    if (!probe->open()) return nullptr;
    return probe;
}

} // namespace SnapTray
