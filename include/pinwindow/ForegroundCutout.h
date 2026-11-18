#ifndef FOREGROUNDCUTOUT_H
#define FOREGROUNDCUTOUT_H

#include <QImage>
#include <QSize>
#include <QtGlobal>

#include <functional>

// Removing the background of a pinned image (#363): a platform segmenter
// marks the foreground subjects, and everything else becomes transparent.
// The canvas keeps its size, so nothing drawn on the image moves.
namespace SnapTray::ForegroundCutout {

enum class Status {
    Success,
    Unsupported,    // no segmentation backend on this platform or OS version
    TooLarge,       // over kMaxPixels; refused before any work is done
    NoSubject,      // the segmenter found nothing in the foreground
    NothingRemoved, // the whole image is foreground: no pixel would change
    Cancelled,      // dropped before it ran
    Failed,
};

// Measured on an Apple M3 with Vision (2026-11): the request itself takes
// 0.1-0.2 s at any size, but its peak memory grows to about 0.5 GB at 33 MP
// and 0.9 GB at 67 MP, before the source, mask and result held here. 40 MP
// still covers a full 8K (7680 x 4320) screen.
constexpr qint64 kMaxPixels = 40'000'000;

// A Grayscale8 coverage mask the size of the image: 255 is foreground,
// 0 background, values between are partial (hair, soft edges).
struct MaskResult {
    QImage mask;
    Status status = Status::Failed;
};

struct Result {
    QImage image;
    // The mask that produced `image` (Success only), for applying the same
    // cut to other copies of these pixels, such as a merged pin's regions.
    QImage mask;
    Status status = Status::Failed;
};

// Must be safe to call off the GUI thread.
using MaskProvider = std::function<MaskResult(const QImage& image)>;

// Success, or TooLarge / Failed for an empty size.
Status validateSize(const QSize& size);

// The source with its alpha multiplied by the mask, as premultiplied ARGB at
// the source's size and device pixel ratio. A mask that keeps no pixel is
// NoSubject; one that changes none is NothingRemoved. Neither returns an image.
Result applyMask(const QImage& source, const QImage& mask);

// The source with its alpha multiplied by the mask, whatever the outcome:
// unlike applyMask, a mask that keeps nothing or changes nothing still gives
// an image. Metadata (ratio, colour space, text) is kept. Null on a size or
// format mismatch.
QImage applyCoverage(const QImage& source, const QImage& mask);

// validateSize, then the provider, then applyMask. Safe on any thread when
// the provider is.
Result run(const QImage& source, const MaskProvider& provider);

} // namespace SnapTray::ForegroundCutout

#endif // FOREGROUNDCUTOUT_H
