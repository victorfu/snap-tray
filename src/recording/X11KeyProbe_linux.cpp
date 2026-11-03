#include "recording/KeyProbe.h"

#include <QDebug>
#include <QLibrary>
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

QString textFor(KeySym keysym);

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
    // Any other printable character (ä, é, Cyrillic, ...): Qt numbers such
    // keys by the upper-case character, so they are recorded too and shortcuts
    // on them keep their key (PR #200 review).
    const QString text = textFor(keysym);
    const QList<uint> codepoints = text.toUcs4();
    if (codepoints.size() == 1 && QChar::isPrint(codepoints.front())) {
        return int(QChar::toUpper(codepoints.front()));
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

// libxkbcommon's full keysym table, for the legacy (pre-Unicode) keysyms of
// Cyrillic, Greek, Hebrew, Thai, Kana... Loaded at run time: Qt's xcb
// platform plugin needs the library already, so it is there wherever this
// probe runs, and no development package is needed to build.
using KeysymToUtf32 = uint32_t (*)(uint32_t);
KeysymToUtf32 keysymToUtf32()
{
    static const KeysymToUtf32 function = [] {
        QLibrary library(QStringLiteral("xkbcommon"), 0);
        return reinterpret_cast<KeysymToUtf32>(library.resolve("xkb_keysym_to_utf32"));
    }();
    return function;
}

// What a keysym types, for the all-keys mode.
QString textFor(KeySym keysym)
{
    if (keysym >= 0x20 && keysym <= 0x7e) return QString(QChar(char16_t(keysym)));
    if (keysym >= 0xa0 && keysym <= 0xff) return QString(QChar(char16_t(keysym)));
    char32_t codepoint = 0;
    if ((keysym & 0xff000000) == 0x01000000) {
        codepoint = char32_t(keysym & 0x00ffffff);
    } else if (const KeysymToUtf32 convert = keysymToUtf32()) {
        // Legacy keysyms (Cyrillic, Greek, ...) and keypad characters.
        codepoint = char32_t(convert(uint32_t(keysym)));
    }
    if (codepoint < 0x20 || codepoint == 0x7f) return QString();
    return QString::fromUcs4(&codepoint, 1);
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
        if (m_xkb) XkbFreeKeyboard(m_xkb, 0, True);
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
        m_localClock.start();
        // State and keymap changes, in order with the key events.
        int opcode = 0, xkbError = 0, xkbMajor = XkbMajorVersion, xkbMinor = XkbMinorVersion;
        if (XkbQueryExtension(m_display, &opcode, &m_xkbEventBase, &xkbError, &xkbMajor, &xkbMinor)) {
            XkbSelectEventDetails(m_display, XkbUseCoreKbd, XkbStateNotify, XkbAllStateComponentsMask,
                                  XkbAllStateComponentsMask);
            XkbSelectEvents(m_display, XkbUseCoreKbd, XkbMapNotifyMask | XkbNewKeyboardNotifyMask,
                            XkbMapNotifyMask | XkbNewKeyboardNotifyMask);
        } else {
            m_xkbEventBase = -1;
        }
        syncState();
        XFlush(m_display);
        m_notifier = new QSocketNotifier(ConnectionNumber(m_display), QSocketNotifier::Read);
        QObject::connect(m_notifier, &QSocketNotifier::activated, this, [this] { drainEvents(); });
        return true;
    }

    QString name() const override { return QStringLiteral("x11"); }

private:
    // Server time to an age in local milliseconds, as X11CursorProbe does:
    // a key queued while the GUI was busy keeps the time it was pressed.
    qint64 ageOf(Time serverTime)
    {
        const qint64 now = m_localClock.elapsed();
        const auto server = static_cast<quint32>(serverTime);
        if (!m_haveTimeReference) {
            m_haveTimeReference = true;
            m_referenceLocalMs = now;
            m_referenceServerMs = server;
            return 0;
        }
        const qint64 happened = m_referenceLocalMs + static_cast<qint32>(server - m_referenceServerMs);
        if (happened > now) {
            m_referenceLocalMs = now;
            m_referenceServerMs = server;
            return 0;
        }
        return now - happened;
    }

    // The keyboard state when the probe starts; from then on it follows the
    // XKB state notifications, which arrive in order with the key events, so
    // a queued press is translated with the state it had (PR #200 review).
    void syncState()
    {
        if (!m_xkb) m_xkb = XkbGetMap(m_display, XkbKeyTypesMask | XkbKeySymsMask | XkbModifierMapMask, XkbUseCoreKbd);
        XkbStateRec state{};
        if (XkbGetState(m_display, XkbUseCoreKbd, &state) != Success) return;
        m_heldMods = state.base_mods | state.latched_mods;
        m_lockedMods = state.locked_mods;
        m_group = state.group;
    }

    void drainEvents()
    {
        while (XPending(m_display) > 0) {
            XEvent event;
            XNextEvent(m_display, &event);
            if (m_xkbEventBase >= 0 && event.type == m_xkbEventBase) {
                const auto* xkb = reinterpret_cast<const XkbEvent*>(&event);
                if (xkb->any.xkb_type == XkbStateNotify) {
                    m_heldMods = xkb->state.base_mods | xkb->state.latched_mods;
                    m_lockedMods = xkb->state.locked_mods;
                    m_group = xkb->state.group;
                } else if (xkb->any.xkb_type == XkbMapNotify || xkb->any.xkb_type == XkbNewKeyboardNotify) {
                    // Another layout or keymap: read it again (Xlib's copy too).
                    if (xkb->any.xkb_type == XkbMapNotify) {
                        XkbMapNotifyEvent mapEvent = xkb->map;
                        XkbRefreshKeyboardMapping(&mapEvent);
                    }
                    if (m_xkb) XkbFreeKeyboard(m_xkb, 0, True);
                    m_xkb = nullptr;
                    syncState();
                }
                continue;
            }
            if (event.type == MappingNotify) {
                XRefreshKeyboardMapping(&event.xmapping);
                continue;
            }
            XGenericEventCookie* cookie = &event.xcookie;
            if (event.type != GenericEvent || cookie->extension != m_xiOpcode || !XGetEventData(m_display, cookie)) {
                continue;
            }
            const auto done = qScopeGuard([this, cookie] { XFreeEventData(m_display, cookie); });
            if (cookie->evtype != XI_RawKeyPress && cookie->evtype != XI_RawKeyRelease) continue;
            const auto* raw = static_cast<const XIRawEvent*>(cookie->data);
            const bool down = cookie->evtype == XI_RawKeyPress;
            const KeyCode code = KeyCode(raw->detail);
            const bool repeat = raw->flags & XIKeyRepeat;
            // The key itself (for shortcut labels): its first level in the
            // active layout, or in the first layout for one without Latin keys.
            const KeySym base = XkbKeycodeToKeysym(m_display, code, m_group, 0);
            int key = qtKeyFor(base);
            if (key == 0 && m_group != 0) key = qtKeyFor(XkbKeycodeToKeysym(m_display, code, 0, 0));
            // What it types: with the state at this event, not the state now
            // (Shift may be up again by the time a queued press is read).
            const KeySym typed = down && !repeat ? typedKeysym(code) : NoSymbol;
            if (const Qt::KeyboardModifier modifier = modifierFor(key)) {
                m_modifiers.setFlag(modifier, down);
                continue;
            }
            if (!down || repeat || key == 0) continue;
            emit keyPressed(key, m_modifiers, textFor(typed), ageOf(raw->time));
        }
    }

    // What the key types with the tracked state: translated through the
    // keyboard's key types with the group and every modifier that picks a
    // level (Shift, Caps Lock, AltGr), not only Shift on the first layout.
    KeySym typedKeysym(KeyCode code)
    {
        if (m_xkb) {
            unsigned int consumed = 0;
            KeySym keysym = NoSymbol;
            if (XkbTranslateKeyCode(m_xkb, code, XkbBuildCoreState(m_heldMods | m_lockedMods, m_group), &consumed,
                                    &keysym)
                && keysym != NoSymbol) {
                return keysym;
            }
        }
        return XkbKeycodeToKeysym(m_display, code, m_group, m_modifiers.testFlag(Qt::ShiftModifier) ? 1 : 0);
    }

    Display* m_display = nullptr;
    XkbDescPtr m_xkb = nullptr;
    unsigned int m_heldMods = 0;
    unsigned int m_lockedMods = 0;
    int m_group = 0;
    int m_xkbEventBase = -1;
    QElapsedTimer m_localClock;
    bool m_haveTimeReference = false;
    qint64 m_referenceLocalMs = 0;
    quint32 m_referenceServerMs = 0;
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
