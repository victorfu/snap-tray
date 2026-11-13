#include "recording/WindowsCursorSupport.h"

#include <vector>

namespace SnapTray::WindowsCursor {

namespace {

// The largest cursor worth converting; the sidecar keeps shapes up to 256 px.
constexpr int kMaxEdge = 1024;

constexpr quint32 kRgbMask = 0x00ffffffu;
constexpr quint32 kAlphaMask = 0xff000000u;
constexpr QRgb kInvertFill = 0xff000000u;    // opaque black
constexpr QRgb kInvertOutline = 0xffffffffu; // opaque white

// Mouse hook messages and X button numbers (winuser.h). Spelled out so this
// file builds everywhere; CursorProbe_win.cpp checks them against windows.h.
constexpr quint32 kWmLButtonDown = 0x0201;
constexpr quint32 kWmLButtonUp = 0x0202;
constexpr quint32 kWmRButtonDown = 0x0204;
constexpr quint32 kWmRButtonUp = 0x0205;
constexpr quint32 kWmMButtonDown = 0x0207;
constexpr quint32 kWmMButtonUp = 0x0208;
constexpr quint32 kWmXButtonDown = 0x020B;
constexpr quint32 kWmXButtonUp = 0x020C;
constexpr quint32 kXButton1 = 1;
constexpr quint32 kXButton2 = 2;

struct MessageEntry {
    quint32 message;
    Qt::MouseButton button;
    bool down;
};
constexpr MessageEntry kButtonMessages[] = {
    {kWmLButtonDown, Qt::LeftButton, true},   {kWmLButtonUp, Qt::LeftButton, false},
    {kWmRButtonDown, Qt::RightButton, true},  {kWmRButtonUp, Qt::RightButton, false},
    {kWmMButtonDown, Qt::MiddleButton, true}, {kWmMButtonUp, Qt::MiddleButton, false},
};

struct XButtonEntry {
    quint32 number;
    Qt::MouseButton button;
};
constexpr XButtonEntry kXButtons[] = {
    {kXButton1, Qt::BackButton},
    {kXButton2, Qt::ForwardButton},
};

enum class Pixel : quint8 { Transparent, Opaque, Inverting };

bool bitSet(quint32 maskPixel) { return (maskPixel & kRgbMask) != 0; }

// Straight alpha to premultiplied, rounding like Qt does.
QRgb premultiplied(quint32 argb)
{
    return qPremultiply(QRgb(argb));
}

} // namespace

Picture pictureFromBitmaps(const Bitmaps& b)
{
    if (b.width <= 0 || b.height <= 0 || b.width > kMaxEdge || b.height > kMaxEdge || !b.mask) return {};
    const int w = b.width;
    const int h = b.height;
    const std::size_t count = std::size_t(w) * std::size_t(h);

    // A colour bitmap with any alpha carries its own transparency (and is
    // not premultiplied); without any, the AND mask decides.
    bool hasAlpha = false;
    if (b.color) {
        for (std::size_t i = 0; i < count && !hasAlpha; ++i) hasAlpha = (b.color[i] & kAlphaMask) != 0;
    }

    std::vector<Pixel> kinds(count, Pixel::Transparent);
    std::vector<QRgb> colors(count, 0);
    bool anyInverting = false;
    bool anyVisible = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (hasAlpha) {
            colors[i] = premultiplied(b.color[i]);
            kinds[i] = qAlpha(colors[i]) ? Pixel::Opaque : Pixel::Transparent;
        } else {
            const bool andBit = bitSet(b.mask[i]);
            // The XOR part: the colour bitmap, or the lower half of a monochrome mask.
            const quint32 xorRgb = b.color ? (b.color[i] & kRgbMask)
                                           : (bitSet(b.mask[count + i]) ? kRgbMask : 0u);
            if (!andBit) {
                kinds[i] = Pixel::Opaque;
                colors[i] = kAlphaMask | xorRgb;
            } else if (xorRgb != 0) {
                kinds[i] = Pixel::Inverting;
                colors[i] = kInvertFill;
                anyInverting = true;
            }
        }
        anyVisible |= kinds[i] != Pixel::Transparent;
    }
    if (!anyVisible) {
        // Nothing to see: a transparent picture, which callers treat as hidden.
        Picture hidden;
        hidden.image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        hidden.image.fill(Qt::transparent);
        return hidden;
    }

    const int border = anyInverting ? kOutlineBorder : 0;
    Picture picture;
    picture.hotspotShift = QPoint(border, border);
    picture.image = QImage(w + 2 * border, h + 2 * border, QImage::Format_ARGB32_Premultiplied);
    picture.image.fill(Qt::transparent);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<QRgb*>(picture.image.scanLine(y + border)) + border;
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            if (kinds[i] != Pixel::Transparent) row[x] = colors[i];
        }
    }
    if (anyInverting) {
        // Light outline: every transparent pixel next to an inverting one.
        const auto inverting = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < w && y < h
                && kinds[std::size_t(y) * std::size_t(w) + std::size_t(x)] == Pixel::Inverting;
        };
        const auto transparent = [&](int x, int y) {
            return x < 0 || y < 0 || x >= w || y >= h
                || kinds[std::size_t(y) * std::size_t(w) + std::size_t(x)] == Pixel::Transparent;
        };
        for (int y = -border; y < h + border; ++y) {
            auto* row = reinterpret_cast<QRgb*>(picture.image.scanLine(y + border)) + border;
            for (int x = -border; x < w + border; ++x) {
                if (!transparent(x, y)) continue;
                bool touches = false;
                for (int dy = -1; dy <= 1 && !touches; ++dy) {
                    for (int dx = -1; dx <= 1 && !touches; ++dx) touches = inverting(x + dx, y + dy);
                }
                if (touches) row[x] = kInvertOutline;
            }
        }
    }
    return picture;
}

std::optional<ButtonChange> buttonChangeFromHookMessage(quint32 message, quint32 mouseData)
{
    for (const auto& entry : kButtonMessages) {
        if (entry.message == message) return ButtonChange{entry.button, entry.down};
    }
    if (message == kWmXButtonDown || message == kWmXButtonUp) {
        const quint32 number = (mouseData >> 16) & 0xffffu; // HIWORD
        for (const auto& entry : kXButtons) {
            if (entry.number == number) return ButtonChange{entry.button, message == kWmXButtonDown};
        }
    }
    return std::nullopt;
}

qint64 tickAgeMs(quint32 nowTicks, quint32 eventTicks)
{
    // Signed 32-bit difference: right across the wrap, negative when the
    // event is stamped after `now`.
    const qint32 age = static_cast<qint32>(nowTicks - eventTicks);
    return age > 0 ? age : 0;
}

} // namespace SnapTray::WindowsCursor
