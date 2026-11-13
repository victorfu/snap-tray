#pragma once

#include <QImage>
#include <QPoint>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

// The parts of the Windows cursor probe (#321) that need no Windows API:
// turning a cursor's bitmaps into a picture, naming a mouse hook message, and
// the queue between the hook thread and the GUI thread. They are built and
// tested on every platform; CursorProbe_win.cpp supplies the Win32 calls.
namespace SnapTray::WindowsCursor {

// A cursor as Windows hands it out (GetIconInfo + GetDIBits), every pixel a
// 32-bit 0xAARRGGBB value, rows top to bottom.
struct Bitmaps {
    int width = 0;
    int height = 0;                 // of the visible picture
    // Colour cursors: width * height pixels. Null for a monochrome cursor.
    const quint32* color = nullptr;
    // Colour cursors: width * height pixels, the AND mask. Monochrome
    // cursors: width * 2 * height pixels, the AND mask above the XOR mask.
    // A mask bit is set when the pixel's RGB is not black.
    const quint32* mask = nullptr;
};

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
// primary button, as CursorProbe::buttonChanged promises.
std::optional<ButtonChange> buttonChangeFromHookMessage(quint32 message, quint32 mouseData);

// Fixed-size queue from one writer thread to one reader thread, without
// locks or allocation: the low-level hook callback must return at once, or
// Windows delays every mouse event (and drops a hook that is too slow).
// push() fails when full; nothing is overwritten.
template <typename T, std::size_t Capacity>
class SpscQueue {
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

} // namespace SnapTray::WindowsCursor
