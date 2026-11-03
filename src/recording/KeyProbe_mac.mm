#include "recording/KeyProbe.h"
#import <Carbon/Carbon.h>
#import <CoreGraphics/CoreGraphics.h>
#include <mach/mach_time.h>
#include <array>

namespace SnapTray {
namespace {
struct NamedKey { CGKeyCode native; int qt; };
constexpr NamedKey namedKeys[] = {
    {kVK_Return, Qt::Key_Return}, {kVK_ANSI_KeypadEnter, Qt::Key_Enter}, {kVK_Tab, Qt::Key_Tab},
    {kVK_Space, Qt::Key_Space}, {kVK_Delete, Qt::Key_Backspace}, {kVK_ForwardDelete, Qt::Key_Delete},
    {kVK_Escape, Qt::Key_Escape}, {kVK_LeftArrow, Qt::Key_Left}, {kVK_RightArrow, Qt::Key_Right},
    {kVK_UpArrow, Qt::Key_Up}, {kVK_DownArrow, Qt::Key_Down}, {kVK_Home, Qt::Key_Home},
    {kVK_End, Qt::Key_End}, {kVK_PageUp, Qt::Key_PageUp}, {kVK_PageDown, Qt::Key_PageDown},
    {kVK_F1, Qt::Key_F1}, {kVK_F2, Qt::Key_F2}, {kVK_F3, Qt::Key_F3}, {kVK_F4, Qt::Key_F4},
    {kVK_F5, Qt::Key_F5}, {kVK_F6, Qt::Key_F6}, {kVK_F7, Qt::Key_F7}, {kVK_F8, Qt::Key_F8},
    {kVK_F9, Qt::Key_F9}, {kVK_F10, Qt::Key_F10}, {kVK_F11, Qt::Key_F11}, {kVK_F12, Qt::Key_F12},
    {kVK_F13, Qt::Key_F13}, {kVK_F14, Qt::Key_F14}, {kVK_F15, Qt::Key_F15}, {kVK_F16, Qt::Key_F16},
    {kVK_F17, Qt::Key_F17}, {kVK_F18, Qt::Key_F18}, {kVK_F19, Qt::Key_F19}, {kVK_F20, Qt::Key_F20}
};
QString unmodifiedText(CGKeyCode code)
{
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source) return {};
    auto data = static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    UniChar chars[8]{};
    UniCharCount length = 0;
    UInt32 dead = 0;
    OSStatus result = paramErr;
    if (data) result = UCKeyTranslate(reinterpret_cast<const UCKeyboardLayout*>(CFDataGetBytePtr(data)),
        code, kUCKeyActionDown, 0, LMGetKbdType(), (1u << kUCKeyTranslateNoDeadKeysBit), &dead, 8, &length, chars);
    CFRelease(source);
    return result == noErr ? QString::fromUtf16(reinterpret_cast<const char16_t*>(chars), qsizetype(length)) : QString{};
}

class MacKeyProbe final : public KeyProbe {
public:
    ~MacKeyProbe() override {
        if (m_source) { CFRunLoopRemoveSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes); CFRelease(m_source); }
        if (m_tap) { CGEventTapEnable(m_tap, false); CFMachPortInvalidate(m_tap); CFRelease(m_tap); }
    }
    bool initialize() {
        // Only called after the user opted in for this recording. Merely
        // showing capability/settings never prompts for Input Monitoring.
        if (!CGPreflightListenEventAccess() && !CGRequestListenEventAccess()) return false;
        m_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
            CGEventMaskBit(kCGEventKeyDown), callback, this);
        if (!m_tap) return false;
        m_source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_tap, 0);
        if (!m_source) return false;
        CFRunLoopAddSource(CFRunLoopGetMain(), m_source, kCFRunLoopCommonModes);
        CGEventTapEnable(m_tap, true);
        return true;
    }
    bool secureInputActive() const override { return IsSecureEventInputEnabled(); }
    bool detectsSecureInput() const override { return true; }
    QString name() const override { return QStringLiteral("macOS Input Monitoring"); }
private:
    static CGEventRef callback(CGEventTapProxy, CGEventType type, CGEventRef event, void* context) {
        auto* self = static_cast<MacKeyProbe*>(context);
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            if (CGPreflightListenEventAccess()) CGEventTapEnable(self->m_tap, true);
            return event;
        }
        if (type != kCGEventKeyDown || self->secureInputActive()) return event;
        const auto code = CGKeyCode(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
        int key = 0;
        for (const auto& entry : namedKeys) if (entry.native == code) { key = entry.qt; break; }
        if (!key) {
            const auto points = unmodifiedText(code).toUpper().toUcs4();
            if (points.size() == 1 && QChar::isPrint(points[0])) key = int(points[0]);
        }
        if (!key) return event;
        const auto flags = CGEventGetFlags(event);
        Qt::KeyboardModifiers modifiers;
        // Qt's macOS convention: Command is Control, physical Control is Meta.
        if (flags & kCGEventFlagMaskCommand) modifiers |= Qt::ControlModifier;
        if (flags & kCGEventFlagMaskControl) modifiers |= Qt::MetaModifier;
        if (flags & kCGEventFlagMaskAlternate) modifiers |= Qt::AltModifier;
        if (flags & kCGEventFlagMaskShift) modifiers |= Qt::ShiftModifier;
        UniChar chars[16]{}; UniCharCount count = 0;
        CGEventKeyboardGetUnicodeString(event, 16, &count, chars);
        const QString text = QString::fromUtf16(reinterpret_cast<const char16_t*>(chars), qsizetype(count));
        mach_timebase_info_data_t scale{}; mach_timebase_info(&scale);
        const auto now = quint64(static_cast<long double>(mach_absolute_time()) * scale.numer / scale.denom);
        const auto timestamp = CGEventGetTimestamp(event);
        emit self->keyPressed(key, modifiers, text, now > timestamp ? qint64((now - timestamp) / 1000000) : 0);
        return event; // Listen-only; never replaces or consumes a key.
    }
    CFMachPortRef m_tap = nullptr;
    CFRunLoopSourceRef m_source = nullptr;
};
}
std::unique_ptr<KeyProbe> createMacKeyProbe()
{
    auto probe = std::make_unique<MacKeyProbe>();
    return probe->initialize() ? std::move(probe) : nullptr;
}
} // namespace SnapTray
