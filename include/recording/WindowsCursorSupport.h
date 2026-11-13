#pragma once

#include <QImage>
#include <QPoint>
#include <QSpan>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

// The parts of the Windows cursor probe (#321) that need no Windows API:
// turning a cursor's bitmaps into a picture, naming a mouse hook message, and
// the queue between the hook thread and the GUI thread. They are built and
// tested on every platform; CursorProbe_win.cpp supplies the Win32 calls.
namespace SnapTray::WindowsCursor {

// A cursor as Windows hands it out (GetIconInfo + GetDIBits), every pixel a
// 32-bit 0xAARRGGBB value, rows top to bottom. The spans say how many pixels
// there really are: bitmaps too short for their size give no picture.
struct Bitmaps {
    int width = 0;
    int height = 0;                 // of the visible picture
    // Colour cursors: width * height pixels. Empty for a monochrome cursor.
    QSpan<const quint32> color;
    // Colour cursors: width * height pixels, the AND mask. Monochrome
    // cursors: width * 2 * height pixels, the AND mask above the XOR mask.
    // A mask bit is set when the pixel's RGB is not black.
    QSpan<const quint32> mask;
};

// The largest cursor edge converted; the sidecar itself keeps shapes up to
// 256 px, so this only keeps a broken bitmap from allocating without bound.
inline constexpr int kMaxCursorEdge = 1024;

struct Picture {
    QImage image;        // premultiplied ARGB; null when the bitmaps are unusable
    QPoint hotspotShift; // added to the cursor's hotspot (the picture can gain a border)
};

// The picture of a cursor. Windows draws a cursor as
// `screen = (screen AND mask) XOR colour`, so besides opaque and transparent
// pixels there are ones that invert the screen under them (the classic text
// I-beam is made of them). A recording draws the pointer over other frames
// than the one it was read on, so inverting pixels are shown dark with a
// light outline, which stays readable on any background. The outline needs a
// pixel of room: such a picture grows by kOutlineBorder on every side.
constexpr int kOutlineBorder = 1;
Picture pictureFromBitmaps(const Bitmaps& bitmaps);

// A button press or release named by a low-level mouse hook message.
struct ButtonChange {
    Qt::MouseButton button = Qt::NoButton;
    bool down = false;
};

// `message` is the hook's WPARAM and `mouseData` MSLLHOOKSTRUCT::mouseData.
// nullopt for anything that is not one of the recorded buttons (moves, the
// wheel, further X buttons).
//
// The hook reports buttons in the user's order already: with the buttons
// swapped in the system settings, pressing the physical right button arrives
// as WM_LBUTTONDOWN. So no swap is applied here, and Left always means the
// primary button, as CursorProbe::buttonChanged promises. Microsoft does not
// document this; it is how the hook is observed to behave, and it is on the
// device checklist (docs/developer/recording-sync-matrix.md).
std::optional<ButtonChange> buttonChangeFromHookMessage(quint32 message, quint32 mouseData);

// MSLLHOOKSTRUCT::flags bits of an event another program synthesised
// (LLMHF_INJECTED, LLMHF_LOWER_IL_INJECTED).
inline constexpr quint32 kHookInjectedFlags = 0x00000001u | 0x00000002u;

// The button change a hook event is recorded as: nullopt for anything that is
// not a recorded button, and for injected events. Leaving injected events out
// keeps clicks that automation or a remote-control tool makes out of the
// record; it also leaves out a user's own clicks when a tool delivers them by
// injection (some remappers and tablet drivers do).
std::optional<ButtonChange> recordedButtonChange(quint32 message, quint32 mouseData, quint32 flags);

// Fixed-size queue from one writer thread to one reader thread, without
// locks or allocation: the low-level hook callback must return at once, or
// Windows delays every mouse event (and drops a hook that is too slow).
// push() fails when full; nothing is overwritten.
template <typename T, std::size_t Capacity>
class SpscQueue {
    // Copying an item must not allocate or run code, and the indices must
    // not hide a lock.
    static_assert(std::is_trivially_copyable_v<T>, "items are copied in the hook callback");
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "the queue must be lock-free");

public:
    bool push(const T& value)
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) % kSlots;
        if (next == m_tail.load(std::memory_order_acquire)) return false;
        m_items[head] = value;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop()
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return std::nullopt;
        const T value = m_items[tail];
        m_tail.store((tail + 1) % kSlots, std::memory_order_release);
        return value;
    }

private:
    static constexpr std::size_t kSlots = Capacity + 1; // one slot stays empty
    std::array<T, kSlots> m_items{};
    std::atomic<std::size_t> m_head{0}; // next slot to write
    std::atomic<std::size_t> m_tail{0}; // next slot to read
};

// How long ago a hook event happened. Both times are GetTickCount()-style
// 32-bit milliseconds, which wrap about every 49 days; a "later" event time
// (clock granularity) counts as now.
qint64 tickAgeMs(quint32 nowTicks, quint32 eventTicks);

// A button change as the hook saw it.
struct HookEvent {
    ButtonChange change;
    quint32 time = 0; // MSLLHOOKSTRUCT::time: in practice GetTickCount() ms when it happened
};

// Button changes on their way from the hook thread to the GUI thread.
// push() belongs to the hook thread and drain() to the GUI thread; nothing
// else is shared.
template <std::size_t Capacity>
class ButtonEventPump {
public:
    // Hook thread. An event that does not fit is counted, not queued.
    void push(const ButtonChange& change, quint32 time)
    {
        if (!m_queue.push(HookEvent{change, time})) m_dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // GUI thread. Calls deliver(change, ageMs) for every queued event, in
    // order, and returns how many events were dropped since the last drain.
    // A drain started from inside deliver does nothing: a handler that polls
    // must not get a queued release before its press has been recorded.
    template <typename Deliver>
    quint32 drain(quint32 nowTicks, Deliver&& deliver)
    {
        if (m_draining) return 0;
        m_draining = true;
        while (const auto event = m_queue.pop()) deliver(event->change, tickAgeMs(nowTicks, event->time));
        m_draining = false;
        return m_dropped.exchange(0, std::memory_order_relaxed);
    }

private:
    SpscQueue<HookEvent, Capacity> m_queue;
    std::atomic<quint32> m_dropped{0};
    bool m_draining = false; // GUI thread only
};

} // namespace SnapTray::WindowsCursor
