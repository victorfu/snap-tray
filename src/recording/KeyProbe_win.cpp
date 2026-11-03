#include "recording/KeyProbe.h"
#include <QMetaObject>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <windows.h>
#include <uiautomation.h>
#include <wrl/client.h>

namespace SnapTray {
namespace {
using Microsoft::WRL::ComPtr;
class WindowsKeyProbe;
thread_local WindowsKeyProbe* activeProbe = nullptr;

HWND focusedWindow()
{
    GUITHREADINFO info{}; info.cbSize = sizeof(info);
    const DWORD thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    return thread && GetGUIThreadInfo(thread, &info) ? info.hwndFocus : nullptr;
}
bool passwordWindow(HWND window)
{
    wchar_t name[32]{};
    return window && GetClassNameW(window, name, 32) && _wcsicmp(name, L"Edit") == 0
        && (GetWindowLongPtrW(window, GWL_STYLE) & ES_PASSWORD);
}
int namedKey(DWORD code)
{
    struct Entry { DWORD code; int key; };
    static constexpr Entry keys[] = {
        {VK_RETURN, Qt::Key_Return}, {VK_ESCAPE, Qt::Key_Escape}, {VK_TAB, Qt::Key_Tab},
        {VK_BACK, Qt::Key_Backspace}, {VK_DELETE, Qt::Key_Delete}, {VK_INSERT, Qt::Key_Insert},
        {VK_HOME, Qt::Key_Home}, {VK_END, Qt::Key_End}, {VK_PRIOR, Qt::Key_PageUp},
        {VK_NEXT, Qt::Key_PageDown}, {VK_LEFT, Qt::Key_Left}, {VK_RIGHT, Qt::Key_Right},
        {VK_UP, Qt::Key_Up}, {VK_DOWN, Qt::Key_Down}, {VK_SPACE, Qt::Key_Space},
        {VK_PAUSE, Qt::Key_Pause}, {VK_SNAPSHOT, Qt::Key_Print}
    };
    for (const auto& entry : keys) if (entry.code == code) return entry.key;
    if (code >= VK_F1 && code <= VK_F24) return Qt::Key_F1 + int(code - VK_F1);
    return 0;
}

class WindowsKeyProbe final : public KeyProbe {
    struct Press {
        DWORD code = 0, scan = 0, time = 0;
        HWND focus = nullptr;
        HKL layout = nullptr;
        quint64 focusEpoch = 0;
        std::array<BYTE, 256> state{};
        Qt::KeyboardModifiers modifiers;
    };
public:
    ~WindowsKeyProbe() override {
        if (m_hook) UnhookWindowsHookEx(m_hook);
        if (m_focusHook) UnhookWinEvent(m_focusHook);
        if (activeProbe == this) activeProbe = nullptr;
        { std::lock_guard<std::mutex> lock(m_mutex); m_stop = true; m_pending.clear(); }
        m_changed.notify_one();
        if (m_worker.joinable()) m_worker.join();
    }
    bool initialize() {
        if (activeProbe) return false;
        m_worker = std::thread([this] { readKeys(); });
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_changed.wait(lock, [this] { return m_ready; });
            if (!m_automationAvailable) return false;
        }
        activeProbe = this;
        m_focusHook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr,
            focusChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
        if (!m_focusHook) return false;
        m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard, GetModuleHandleW(nullptr), 0);
        return m_hook != nullptr;
    }
    QString name() const override { return QStringLiteral("Windows keyboard hook"); }
    bool detectsSecureInput() const override { return true; }
    bool secureInputActive() const override {
        const HWND focused = focusedWindow();
        return !focused || passwordWindow(focused) || m_secure.load();
    }
private:
    static void CALLBACK focusChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
        if (activeProbe) { ++activeProbe->m_focusEpoch; activeProbe->m_secure = true; }
    }
    static LRESULT CALLBACK keyboard(int code, WPARAM message, LPARAM data) {
        auto* self = activeProbe;
        if (code == HC_ACTION && self && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
            const auto& native = *reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
            const HWND focus = focusedWindow();
            if (!(native.flags & LLKHF_INJECTED) && focus && !passwordWindow(focus) && native.vkCode < 256) {
                Press p;
                p.code = native.vkCode; p.scan = native.scanCode; p.time = native.time;
                p.focus = focus; p.focusEpoch = self->m_focusEpoch.load();
                p.layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
                // The low-level callback runs before async state is updated
                // for this key, so apply its down bit explicitly.
                for (int i = 0; i < 256; ++i) p.state[size_t(i)] = (GetAsyncKeyState(i) & 0x8000) ? 0x80 : 0;
                p.state[VK_CAPITAL] |= BYTE(GetKeyState(VK_CAPITAL) & 1);
                p.state[VK_NUMLOCK] |= BYTE(GetKeyState(VK_NUMLOCK) & 1);
                p.state[p.code] |= 0x80;
                if (p.state[VK_CONTROL] & 0x80) p.modifiers |= Qt::ControlModifier;
                if (p.state[VK_MENU] & 0x80) p.modifiers |= Qt::AltModifier;
                if (p.state[VK_SHIFT] & 0x80) p.modifiers |= Qt::ShiftModifier;
                if ((p.state[VK_LWIN] | p.state[VK_RWIN]) & 0x80) p.modifiers |= Qt::MetaModifier;
                { std::lock_guard<std::mutex> lock(self->m_mutex);
                    // Bound transient input when an accessibility provider is
                    // slow. No text is decoded or persisted in the hook.
                    if (self->m_pending.size() >= 64) self->m_pending.clear();
                    self->m_pending.push_back(p);
                }
                self->m_changed.notify_one();
            }
        }
        // Never swallow input and never call an accessibility provider here.
        return CallNextHookEx(nullptr, code, message, data);
    }
    void readKeys() {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IUIAutomation2> automation;
        HRESULT hr = FAILED(initialized) ? initialized
            : CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
        if (SUCCEEDED(hr)) {
            automation->put_ConnectionTimeout(200);
            automation->put_TransactionTimeout(200);
        }
        { std::lock_guard<std::mutex> lock(m_mutex); m_automationAvailable = SUCCEEDED(hr); m_ready = true; }
        m_changed.notify_one();
        if (SUCCEEDED(hr)) for (;;) {
            Press p;
            { std::unique_lock<std::mutex> lock(m_mutex);
                m_changed.wait(lock, [this] { return m_stop || !m_pending.empty(); });
                if (m_stop) break;
                p = m_pending.front(); m_pending.pop_front();
            }
            if (DWORD(GetTickCount() - p.time) > 250 || p.focusEpoch != m_focusEpoch.load()
                || p.focus != focusedWindow()) continue;
            ComPtr<IUIAutomationElement> focused;
            BOOL password = TRUE;
            const bool readable = SUCCEEDED(automation->GetFocusedElement(&focused)) && focused
                && SUCCEEDED(focused->get_CurrentIsPassword(&password));
            const bool secure = !readable || password || passwordWindow(p.focus);
            m_secure = secure;
            if (secure || p.focusEpoch != m_focusEpoch.load() || p.focus != focusedWindow()) continue;
            wchar_t chars[16]{};
            // Flag 4 asks ToUnicodeEx not to alter the application's dead-key
            // state (Windows 10+, the supported Windows target).
            const int count = ToUnicodeEx(p.code, p.scan, p.state.data(), chars, 16, 4, p.layout);
            const QString text = count > 0 ? QString::fromWCharArray(chars, count) : QString{};
            int key = namedKey(p.code);
            if (!key) {
                auto plain = p.state;
                plain[VK_CONTROL] = plain[VK_LCONTROL] = plain[VK_RCONTROL] = 0;
                plain[VK_MENU] = plain[VK_LMENU] = plain[VK_RMENU] = 0;
                const int letters = ToUnicodeEx(p.code, p.scan, plain.data(), chars, 16, 4, p.layout);
                const auto points = letters > 0 ? QString::fromWCharArray(chars, letters).toUpper().toUcs4() : QList<uint>{};
                if (points.size() == 1 && QChar::isPrint(points[0])) key = int(points[0]);
            }
            if (!key) continue;
            QMetaObject::invokeMethod(this, [this, p, key, text] {
                // A queued result must still belong to the same focus, and
                // must not outlive a pause/resume boundary (ageMs handles it).
                if (p.focusEpoch != m_focusEpoch.load() || p.focus != focusedWindow() || secureInputActive()) return;
                emit keyPressed(key, p.modifiers, text, DWORD(GetTickCount() - p.time));
            }, Qt::QueuedConnection);
        }
        automation.Reset();
        if (SUCCEEDED(initialized)) CoUninitialize();
    }
    HHOOK m_hook = nullptr;
    HWINEVENTHOOK m_focusHook = nullptr;
    std::atomic<quint64> m_focusEpoch{0};
    std::atomic<bool> m_secure{true};
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::deque<Press> m_pending;
    std::thread m_worker;
    bool m_stop = false, m_ready = false, m_automationAvailable = false;
};
}
std::unique_ptr<KeyProbe> createWindowsKeyProbe()
{
    auto probe = std::make_unique<WindowsKeyProbe>();
    return probe->initialize() ? std::move(probe) : nullptr;
}
} // namespace SnapTray
