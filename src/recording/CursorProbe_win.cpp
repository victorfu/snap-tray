#include "recording/CursorProbe.h"
#include "recording/WindowsCursorSupport.h"

#include <QDebug>
#include <QScopeGuard>
#include <QThread>

#include <array>
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

// WindowsCursorSupport spells these out to build everywhere.
static_assert(WM_LBUTTONDOWN == 0x0201 && WM_LBUTTONUP == 0x0202, "left button messages");
static_assert(WM_RBUTTONDOWN == 0x0204 && WM_RBUTTONUP == 0x0205, "right button messages");
static_assert(WM_MBUTTONDOWN == 0x0207 && WM_MBUTTONUP == 0x0208, "middle button messages");
static_assert(WM_XBUTTONDOWN == 0x020B && WM_XBUTTONUP == 0x020C, "X button messages");
static_assert(XBUTTON1 == 1 && XBUTTON2 == 2, "X button numbers");
static_assert((LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED) == WindowsCursor::kHookInjectedFlags, "injected flags");

// Button events waiting for the GUI thread. The recorder polls every 16 ms,
// so a handful is the normal load. A paused recording does not poll: its
// events pile up here and are discarded on resume, whether they fit or not.
constexpr std::size_t kQueueCapacity = 256;
constexpr int kBitsPerPixel = 32;

using ButtonPump = WindowsCursor::ButtonEventPump<kQueueCapacity>;

// The hook callback has no context argument. A low-level hook runs on the
// thread that installed it, so that thread's own pointer is enough.
thread_local ButtonPump* hookTarget = nullptr;

LRESULT CALLBACK mouseHook(int code, WPARAM message, LPARAM data)
{
    ButtonPump* const target = hookTarget;
    if (code == HC_ACTION && target) {
        const auto& native = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        // No allocation, lock or I/O here: Windows delays every mouse event
        // until this returns, and drops a hook that is too slow.
        if (const auto change = WindowsCursor::recordedButtonChange(
                quint32(message), quint32(native.mouseData), quint32(native.flags))) {
            target->push(*change, quint32(native.time));
        }
    }
    // Never swallow input.
    return CallNextHookEx(nullptr, code, message, data);
}

// What the hook thread reports once it has tried to install the hook.
struct HookStart {
    bool installed = false;
    DWORD error = 0; // GetLastError() when it was refused
};

// Runs the hook on a thread of its own: the hook is serviced by the message
// loop of the thread that installed it, and on the GUI thread any stall there
// (a slow paint, a blocking call) would make the whole desktop's pointer lag.
// Runs until `stop` is signalled, then removes the hook.
void runMouseHook(ButtonPump* pump, HANDLE stop, std::promise<HookStart> started)
{
    // It does almost nothing, but what it does delays the user's mouse. A
    // refusal only leaves it at normal priority.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    hookTarget = pump;
    const HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(nullptr), 0);
    const DWORD refusal = hook ? 0 : GetLastError();
    started.set_value(HookStart{hook != nullptr, refusal});
    if (hook) {
        // The hook is called while this thread waits for and takes messages.
        // Stopping is an event, not a posted message: setting it cannot fail
        // and does not depend on this thread's message queue.
        for (;;) {
            const DWORD woke = MsgWaitForMultipleObjects(1, &stop, FALSE, INFINITE, QS_ALLINPUT);
            if (woke != WAIT_OBJECT_0 + 1) break; // stop, or the wait failed
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                DispatchMessageW(&message);
            }
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
    return edge > 0 && edge <= WindowsCursor::kMaxCursorEdge;
}

// Why a cursor could not be turned into a picture. Each is logged once per
// run: a cursor that fails does so on every poll that sees it.
enum class ShapeFailure { Copy, Info, MaskHeader, Size, MaskPixels, ColorHeader, ColorPixels, Picture, Count };

constexpr std::array<const char*, std::size_t(ShapeFailure::Count)> kShapeFailureNames = {
    "CopyIcon failed",
    "GetIconInfo failed",
    "the mask bitmap cannot be described",
    "the cursor has an unusable size",
    "the mask bitmap cannot be read",
    "the colour bitmap does not match the mask",
    "the colour bitmap cannot be read",
    "the bitmaps make no picture",
};

std::optional<CursorProbe::Shape> shapeFailed(ShapeFailure failure, DWORD error)
{
    static std::array<bool, std::size_t(ShapeFailure::Count)> reported{};
    bool& seen = reported[std::size_t(failure)];
    if (!seen) {
        seen = true;
        qWarning() << "WindowsCursorProbe: a pointer shape is recorded as unknown:"
                   << kShapeFailureNames[std::size_t(failure)] << "(error" << error << ")";
    }
    return std::nullopt;
}

// Everything but m_pump belongs to the GUI thread. m_pump is shared with the
// hook thread, which only pushes into it.
class WindowsCursorProbe final : public CursorProbe
{
public:
    WindowsCursorProbe()
    {
        m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_stop) {
            qWarning() << "WindowsCursorProbe: no stop event (error" << GetLastError()
                       << "); clicks are not recorded";
            return;
        }
        std::promise<HookStart> started;
        std::future<HookStart> start = started.get_future();
        m_hookThread = std::thread(runMouseHook, &m_pump, m_stop, std::move(started));
        const HookStart result = start.get();
        m_clicks = result.installed;
        if (!m_clicks) {
            m_hookThread.join();
            qWarning() << "WindowsCursorProbe: the mouse hook was refused (error" << result.error
                       << "); clicks are not recorded";
        }
    }

    ~WindowsCursorProbe() override
    {
        if (m_hookThread.joinable()) {
            // The hook is removed by its own thread, right after its loop ends.
            SetEvent(m_stop);
            m_hookThread.join();
        }
        if (m_stop) CloseHandle(m_stop);
    }

    Capabilities capabilities() const override { return {true, m_clicks}; }
    QString name() const override { return QStringLiteral("windows"); }

    std::optional<State> poll() override
    {
        Q_ASSERT(thread() == QThread::currentThread());
        drainEvents();
        CURSORINFO info{};
        info.cbSize = sizeof(info);
        // Fails while another desktop has the input (the lock screen, a UAC
        // prompt): the pointer cannot be read then.
        const bool readable = GetCursorInfo(&info) != 0;
        if (readable != m_readable) {
            m_readable = readable;
            if (readable) qWarning() << "WindowsCursorProbe: the pointer can be read again";
            else qWarning() << "WindowsCursorProbe: the pointer cannot be read (error" << GetLastError()
                            << "); nothing is recorded until it can";
        }
        if (!readable) return std::nullopt;
        // Anything but "showing" is hidden, also a pointer Windows suppresses
        // for touch or pen input.
        const bool showing = (info.flags & CURSOR_SHOWING) != 0;
        if (!m_polled || info.hCursor != m_cursor || showing != m_showing) {
            m_polled = true;
            m_cursor = info.hCursor;
            m_showing = showing;
            ++m_serial;
        }
        // Physical pixels, because the process is per-monitor DPI aware (Qt 6
        // makes it so, and nothing in SnapTray overrides that).
        return State{QPoint(info.ptScreenPos.x, info.ptScreenPos.y), true, m_serial};
    }

    std::optional<Shape> currentShape() override
    {
        // The cursor poll() last saw, so the shape matches its serial.
        if (!m_polled) return std::nullopt;
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
        const quint32 dropped = m_pump.drain(
            quint32(GetTickCount()), [this](const WindowsCursor::ButtonChange& change, qint64 ageMs) {
                emit buttonChanged(change.button, change.down, ageMs);
            });
        if (dropped > 0) {
            qWarning() << "WindowsCursorProbe:" << dropped
                       << "button events did not fit the queue and were dropped";
            emit buttonEventsLost();
        }
    }

    ButtonPump m_pump;
    HANDLE m_stop = nullptr;
    std::thread m_hookThread;
    bool m_clicks = false;   // the hook was installed
    bool m_polled = false;   // m_cursor and m_showing are what poll() saw
    bool m_readable = true;  // the last GetCursorInfo worked
    HCURSOR m_cursor = nullptr;
    bool m_showing = false;
    quint64 m_serial = 0;    // the first poll makes it 1; 0 means "unknown" to the recorder
};

} // namespace

std::optional<CursorProbe::Shape> windowsCursorShape(void* cursorHandle)
{
    if (!cursorHandle) return std::nullopt;
    // A private copy: the application owning the cursor may destroy it at any time.
    const HICON copy = CopyIcon(static_cast<HICON>(cursorHandle));
    if (!copy) return shapeFailed(ShapeFailure::Copy, GetLastError());
    ICONINFO icon{};
    const bool read = GetIconInfo(copy, &icon) != 0;
    const DWORD readError = read ? 0 : GetLastError();
    DestroyIcon(copy);
    if (!read) return shapeFailed(ShapeFailure::Info, readError);
    // GetIconInfo hands over two bitmaps that are ours to delete.
    const auto cleanup = qScopeGuard([&icon] {
        if (icon.hbmMask) DeleteObject(icon.hbmMask);
        if (icon.hbmColor) DeleteObject(icon.hbmColor);
    });
    BITMAP mask{};
    if (!icon.hbmMask || !GetObjectW(icon.hbmMask, sizeof(mask), &mask)) {
        return shapeFailed(ShapeFailure::MaskHeader, GetLastError());
    }
    // A monochrome cursor has no colour bitmap: its mask is twice as high,
    // the AND mask above the XOR mask.
    const LONG maskRows = icon.hbmColor ? 1 : 2;
    if (!usableEdge(mask.bmWidth) || !usableEdge(mask.bmHeight / maskRows) || mask.bmHeight % maskRows != 0) {
        return shapeFailed(ShapeFailure::Size, 0);
    }
    WindowsCursor::Bitmaps bitmaps;
    bitmaps.width = int(mask.bmWidth);
    bitmaps.height = int(mask.bmHeight / maskRows);
    const std::vector<quint32> maskPixels = readPixels(icon.hbmMask, bitmaps.width, int(mask.bmHeight));
    if (maskPixels.empty()) return shapeFailed(ShapeFailure::MaskPixels, GetLastError());
    bitmaps.mask = maskPixels;
    std::vector<quint32> colorPixels;
    if (icon.hbmColor) {
        BITMAP color{};
        if (!GetObjectW(icon.hbmColor, sizeof(color), &color) || color.bmWidth != mask.bmWidth
            || color.bmHeight != mask.bmHeight) {
            return shapeFailed(ShapeFailure::ColorHeader, 0);
        }
        colorPixels = readPixels(icon.hbmColor, bitmaps.width, bitmaps.height);
        if (colorPixels.empty()) return shapeFailed(ShapeFailure::ColorPixels, GetLastError());
        bitmaps.color = colorPixels;
    }
    const WindowsCursor::Picture picture = WindowsCursor::pictureFromBitmaps(bitmaps);
    if (picture.image.isNull()) return shapeFailed(ShapeFailure::Picture, 0);
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
