#include "recording/CursorProbe.h"
#include "recording/WindowsCursorSupport.h"

#include <QDebug>
#include <QScopeGuard>

#include <atomic>
#include <future>
#include <thread>
#include <vector>

#include <windows.h>

// The pointer on Windows (#321): where it is, what it looks like and when a
// button is pressed, so pointer effects and Auto Zoom have the same record
// as on X11. Everything is read passively: the low-level mouse hook only
// listens and always passes the event on.
namespace SnapTray {

namespace {

// buttonChangeFromHookMessage() spells these out to build everywhere.
static_assert(WM_LBUTTONDOWN == 0x0201 && WM_LBUTTONUP == 0x0202, "left button messages");
static_assert(WM_RBUTTONDOWN == 0x0204 && WM_RBUTTONUP == 0x0205, "right button messages");
static_assert(WM_MBUTTONDOWN == 0x0207 && WM_MBUTTONUP == 0x0208, "middle button messages");
static_assert(WM_XBUTTONDOWN == 0x020B && WM_XBUTTONUP == 0x020C, "X button messages");
static_assert(XBUTTON1 == 1 && XBUTTON2 == 2, "X button numbers");

// Button events waiting for the GUI thread. The recorder polls every 16 ms,
// so a handful is the normal load; the rest is room for a paused recording.
constexpr std::size_t kQueueCapacity = 256;
// The largest cursor bitmap read; larger ones are recorded as "unknown".
constexpr int kMaxCursorEdge = 1024;
constexpr int kBitsPerPixel = 32;

struct HookEvent {
    quint32 message = 0;
    quint32 mouseData = 0;
    quint32 time = 0; // GetTickCount() ms when it happened
};

// What the hook thread and the probe share.
struct HookShared {
    WindowsCursor::SpscQueue<HookEvent, kQueueCapacity> queue;
    std::atomic<bool> overflowed{false};
};

// The hook callback has no context argument. A low-level hook runs on the
// thread that installed it, so that thread's own pointer is enough.
thread_local HookShared* hookTarget = nullptr;

LRESULT CALLBACK mouseHook(int code, WPARAM message, LPARAM data)
{
    HookShared* const target = hookTarget;
    if (code == HC_ACTION && target) {
        const auto& native = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        // Only what the user did: not clicks another program synthesised.
        constexpr DWORD kInjected = LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED;
        if (!(native.flags & kInjected)
            && WindowsCursor::buttonChangeFromHookMessage(quint32(message), quint32(native.mouseData))) {
            // No allocation, lock or I/O here: Windows delays every mouse
            // event until this returns, and drops a hook that is too slow.
            if (!target->queue.push({quint32(message), quint32(native.mouseData), quint32(native.time)})) {
                target->overflowed.store(true, std::memory_order_relaxed);
            }
        }
    }
    // Never swallow input.
    return CallNextHookEx(nullptr, code, message, data);
}

// Runs the hook on a thread of its own: the hook is serviced by the message
// loop of the thread that installed it, and on the GUI thread any stall there
// (a slow paint, a blocking call) would make the whole desktop's pointer lag.
// Reports its thread id through `started`, or 0 when the hook was refused.
void runMouseHook(HookShared* shared, std::promise<DWORD> started)
{
    MSG message;
    // Creates this thread's message queue, so WM_QUIT can be posted to it.
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    // It does almost nothing, but what it does delays the user's mouse.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    hookTarget = shared;
    const HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(nullptr), 0);
    started.set_value(hook ? GetCurrentThreadId() : 0);
    if (hook) {
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            DispatchMessageW(&message);
        }
        UnhookWindowsHookEx(hook);
    }
    hookTarget = nullptr;
}

// A bitmap's pixels as 32-bit 0xAARRGGBB, top row first. Empty on failure.
std::vector<quint32> readPixels(HBITMAP bitmap, int width, int height)
{
    std::vector<quint32> pixels(std::size_t(width) * std::size_t(height));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = kBitsPerPixel;
    info.bmiHeader.biCompression = BI_RGB;
    const HDC screen = GetDC(nullptr);
    if (!screen) return {};
    const int lines = GetDIBits(screen, bitmap, 0, UINT(height), pixels.data(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (lines != height) return {};
    return pixels;
}

bool usableEdge(LONG edge)
{
    return edge > 0 && edge <= kMaxCursorEdge;
}

class WindowsCursorProbe final : public CursorProbe
{
public:
    WindowsCursorProbe()
    {
        std::promise<DWORD> started;
        std::future<DWORD> threadId = started.get_future();
        m_hookThread = std::thread(runMouseHook, &m_shared, std::move(started));
        m_hookThreadId = threadId.get();
        if (!m_hookThreadId) {
            m_hookThread.join();
            qWarning() << "WindowsCursorProbe: the mouse hook was refused; clicks are not recorded";
        }
    }

    ~WindowsCursorProbe() override
    {
        if (m_hookThread.joinable()) {
            // The hook is removed by its own thread, right after its loop ends.
            PostThreadMessageW(m_hookThreadId, WM_QUIT, 0, 0);
            m_hookThread.join();
        }
    }

    Capabilities capabilities() const override { return {true, m_hookThreadId != 0}; }
    QString name() const override { return QStringLiteral("windows"); }

    std::optional<State> poll() override
    {
        drainEvents();
        CURSORINFO info{};
        info.cbSize = sizeof(info);
        // Fails while another desktop has the input (the lock screen, a UAC
        // prompt): the pointer cannot be read then.
        if (!GetCursorInfo(&info)) return std::nullopt;
        // Anything but "showing" is hidden, also a pointer Windows suppresses
        // for touch or pen input.
        const bool showing = (info.flags & CURSOR_SHOWING) != 0;
        if (info.hCursor != m_cursor || showing != m_showing) {
            m_cursor = info.hCursor;
            m_showing = showing;
            ++m_serial;
        }
        // Screen coordinates of a per-monitor DPI aware process are physical pixels.
        return State{QPoint(info.ptScreenPos.x, info.ptScreenPos.y), true, m_serial};
    }

    std::optional<Shape> currentShape() override
    {
        // The cursor poll() last saw, so the shape matches its serial.
        if (!m_showing || !m_cursor) {
            // A fully transparent picture is how a hidden pointer is reported.
            Shape hidden;
            hidden.image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            hidden.image.fill(Qt::transparent);
            return hidden;
        }
        return windowsCursorShape(m_cursor);
    }

private:
    void drainEvents()
    {
        // A button signal's handler polls the position, which drains again:
        // a nested drain would deliver a queued release inside the press
        // handler, before the press is recorded (as in the X11 probe).
        if (m_draining) return;
        m_draining = true;
        const auto done = qScopeGuard([this] { m_draining = false; });
        while (const auto event = m_shared.queue.pop()) {
            const auto change = WindowsCursor::buttonChangeFromHookMessage(event->message, event->mouseData);
            if (!change) continue;
            emit buttonChanged(change->button, change->down,
                               WindowsCursor::tickAgeMs(quint32(GetTickCount()), event->time));
        }
        if (m_shared.overflowed.exchange(false, std::memory_order_relaxed) && !m_overflowReported) {
            m_overflowReported = true;
            qWarning() << "WindowsCursorProbe: the button event queue filled up; some button events were dropped";
        }
    }

    HookShared m_shared;
    std::thread m_hookThread;
    DWORD m_hookThreadId = 0; // 0: no hook, so no clicks
    HCURSOR m_cursor = nullptr;
    bool m_showing = false;
    quint64 m_serial = 1; // 0 means "unknown" to the recorder
    bool m_draining = false;
    bool m_overflowReported = false;
};

} // namespace

std::optional<CursorProbe::Shape> windowsCursorShape(void* cursorHandle)
{
    if (!cursorHandle) return std::nullopt;
    // A private copy: the application owning the cursor may destroy it at any time.
    const HICON copy = CopyIcon(static_cast<HICON>(cursorHandle));
    if (!copy) return std::nullopt;
    ICONINFO icon{};
    const bool read = GetIconInfo(copy, &icon) != 0;
    DestroyIcon(copy);
    if (!read) return std::nullopt;
    // GetIconInfo hands over two bitmaps that are ours to delete.
    const auto cleanup = qScopeGuard([&icon] {
        if (icon.hbmMask) DeleteObject(icon.hbmMask);
        if (icon.hbmColor) DeleteObject(icon.hbmColor);
    });
    BITMAP mask{};
    if (!icon.hbmMask || !GetObjectW(icon.hbmMask, sizeof(mask), &mask)) return std::nullopt;
    // A monochrome cursor has no colour bitmap: its mask is twice as high,
    // the AND mask above the XOR mask.
    const LONG maskRows = icon.hbmColor ? 1 : 2;
    if (!usableEdge(mask.bmWidth) || !usableEdge(mask.bmHeight) || mask.bmHeight % maskRows != 0) {
        return std::nullopt;
    }
    WindowsCursor::Bitmaps bitmaps;
    bitmaps.width = int(mask.bmWidth);
    bitmaps.height = int(mask.bmHeight / maskRows);
    const std::vector<quint32> maskPixels = readPixels(icon.hbmMask, bitmaps.width, int(mask.bmHeight));
    if (maskPixels.empty()) return std::nullopt;
    bitmaps.mask = maskPixels.data();
    std::vector<quint32> colorPixels;
    if (icon.hbmColor) {
        BITMAP color{};
        if (!GetObjectW(icon.hbmColor, sizeof(color), &color) || color.bmWidth != mask.bmWidth
            || color.bmHeight != mask.bmHeight) {
            return std::nullopt;
        }
        colorPixels = readPixels(icon.hbmColor, bitmaps.width, bitmaps.height);
        if (colorPixels.empty()) return std::nullopt;
        bitmaps.color = colorPixels.data();
    }
    const WindowsCursor::Picture picture = WindowsCursor::pictureFromBitmaps(bitmaps);
    if (picture.image.isNull()) return std::nullopt;
    CursorProbe::Shape shape;
    shape.image = picture.image;
    shape.hotspot = QPoint(int(icon.xHotspot), int(icon.yHotspot)) + picture.hotspotShift;
    return shape;
}

std::unique_ptr<CursorProbe> createWindowsCursorProbe()
{
    return std::make_unique<WindowsCursorProbe>();
}

} // namespace SnapTray
