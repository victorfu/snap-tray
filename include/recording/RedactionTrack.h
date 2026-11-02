#pragma once

#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QRectF>

#include <vector>

// Covering sensitive parts of a recording after recording (#162). Masks are
// in source video pixels and source time, and are burned into the source
// content before any crop scaling, zoom or pointer effect, so nothing that
// is masked can reappear through resampling. The original recording is
// never changed; only exports are masked.
namespace SnapTray {

struct RedactionMask {
    enum class Style { Solid, Pixelate, Blur };

    QRect rect;          // source video pixels
    qint64 startMs = 0;  // shown from here...
    qint64 endMs = -1;   // ...to here; -1 = to the end of the recording
    Style style = Style::Solid;

    // Conservative timing: a frame is masked when any part of the time it
    // stays on screen (up to kFrameHoldMs after its timestamp) overlaps the
    // mask, so no frame shows the content at the moment the mask begins.
    bool isActiveAt(qint64 frameMs) const;
    bool operator==(const RedactionMask& o) const;
    bool operator!=(const RedactionMask& o) const { return !(*this == o); }
};

class RedactionTrack {
public:
    // The longest a frame is assumed to stay on screen (a 5 fps GIF frame,
    // or a held frame across a capture gap that the exports re-time).
    static constexpr qint64 kFrameHoldMs = 200;
    static constexpr int kMinMaskSide = 4;
    // Pixel block for Pixelate, and the reduction for Blur, in source pixels.
    static constexpr int kPixelateBlock = 16;
    static constexpr int kBlurReduction = 24;

    std::vector<RedactionMask> masks;

    bool isEmpty() const { return masks.empty(); }
    // Burns the masks active at frameMs into `frame`, which shows the source
    // area `frameSourceRect` (the crop) at frame.size(). Edges are rounded
    // outward: a mask never covers less than its rect.
    void apply(QImage& frame, qint64 frameMs, const QRectF& frameSourceRect) const;

    QJsonObject toJson() const;
    static RedactionTrack fromJson(const QJsonObject& json);
    bool operator==(const RedactionTrack& o) const { return masks == o.masks; }
    bool operator!=(const RedactionTrack& o) const { return !(*this == o); }
};

} // namespace SnapTray
