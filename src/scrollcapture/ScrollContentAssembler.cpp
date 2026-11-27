#include "scrollcapture/ScrollContentAssembler.h"
#include "scrollcapture/FrameRowHashes.h"
#include "scrollcapture/RowHashShiftEstimator.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace snaptray::scrollcapture {
namespace {
constexpr int kBytesPerPixel = sizeof(QRgb);

ScrollContentResult fail(const char* reason)
{
    ScrollContentResult result;
    result.error = QLatin1String(reason);
    return result;
}

QRect largestCommonRegion(const QVector<int>& requiredTop, const QSize& size)
{
    const int width = size.width();
    const int height = size.height();
    // Largest valid rectangle via histogram stacks, O(width * height).
    // Require a majority of BOTH viewport dimensions, not a lucky tiny patch.
    QRect region;
    qint64 bestArea = 0;
    QVector<int> heights(width + 1, 0);
    QVector<int> tops(width, 0);
    QVector<int> stack;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            tops[x] = std::max(tops[x], requiredTop[y * width + x]);
            heights[x] = y - tops[x] + 1;
        }
        stack.clear();
        for (int x = 0; x <= width; ++x) {
            while (!stack.isEmpty() && heights[stack.last()] > heights[x]) {
                const int h = heights[stack.takeLast()];
                const int left = stack.isEmpty() ? 0 : stack.last() + 1;
                const int w = x - left;
                const qint64 area = qint64(w) * h;
                if (w > width / 2 && h > height / 2 && area > bestArea) {
                    region = QRect(left, y - h + 1, w, h);
                    bestArea = area;
                }
            }
            stack.append(x);
        }
    }
    return region;
}

bool omitsMovingColumns(const QVector<ScrollContentPair>& pairs, const QRect& region, int width)
{
    constexpr int kStripWidth = 32;
    // Compare discarded columns in narrow strips. Blank margins and chrome
    // that stays put (or initially moves with the page before becoming fixed)
    // are expected. Repeated evidence of the same motion outside the crop
    // means an exact crop would still discard part of the scrolling content.
    for (const QRect& side : {QRect(0, region.top(), region.left(), region.height()),
                              QRect(region.right() + 1, region.top(), width - region.right() - 1, region.height())}) {
        for (int x = side.left(); x <= side.right(); x += kStripWidth) {
            const QRect strip(x, side.top(), std::min(kStripWidth, side.right() - x + 1), side.height());
            qsizetype matchingPairs = 0;
            for (const auto& pair : pairs) {
                const auto before = computeRowHashes(pair.before.copy(strip));
                const auto after = computeRowHashes(pair.after.copy(strip));
                const auto motion = estimateVerticalShift(before, after);
                if (motion.status == ShiftEstimate::Status::Found && motion.shift == pair.shift) {
                    if (++matchingPairs > pairs.size() / 2) return true;
                }
            }
        }
    }
    return false;
}

ScrollContentResult assemble(const QVector<ScrollContentPair>& pairs, qint64 maxOutputPixels)
{
    if (pairs.isEmpty() || pairs.first().before.isNull() || maxOutputPixels <= 0) {
        return fail("invalid_input");
    }
    const QSize size = pairs.first().before.size();
    const int width = size.width();
    const int height = size.height();
    if (qint64(width) * height > std::min<qint64>(maxOutputPixels, std::numeric_limits<int>::max())) {
        return fail("pixel_budget_exceeded");
    }
    qint64 totalShift = 0;
    for (const auto& pair : pairs) {
        if (pair.before.size() != size || pair.after.size() != size) {
            return fail("invalid_geometry");
        }
        if (pair.shift <= 0) return fail("non_positive_shift");
        if (pair.shift >= height / 2) return fail("shift_too_large");
        totalShift += pair.shift;
    }
    // For a rectangle ending at row b, pair s compares after[y] to before[y+s]
    // only through y=b-s. A mismatch at y therefore forces its top below y
    // starting at bottom y+s. Keep these constraints separately in bottom-row
    // coordinates, so a short final scroll does not truncate every other pair.
    QVector<int> requiredTop(width * height, 0);
    for (const auto& pair : pairs) {
        const auto before = pair.before.convertToFormat(QImage::Format_ARGB32);
        const auto after = pair.after.convertToFormat(QImage::Format_ARGB32);
        if (before.isNull() || after.isNull()) return fail("allocation_failed");
        for (int y = 0; y < height - pair.shift; ++y) {
            const auto* a = reinterpret_cast<const QRgb*>(before.constScanLine(y + pair.shift));
            const auto* b = reinterpret_cast<const QRgb*>(after.constScanLine(y));
            for (int x = 0; x < width; ++x) {
                if (a[x] != b[x]) {
                    auto& top = requiredTop[(y + pair.shift) * width + x];
                    top = std::max(top, y + 1);
                }
            }
        }
    }
    QRect region = largestCommonRegion(requiredTop, size);
    if (region.isEmpty()) return fail("no_common_content_region");

    // Include inter-pair constraints when choosing the crop. A scrollbar or
    // caret may change between captures while the exact body stays continuous.
    bool seamChanged = false;
    for (qsizetype i = 1; i < pairs.size(); ++i) {
        const auto previous = pairs[i - 1].after.convertToFormat(QImage::Format_ARGB32);
        const auto next = pairs[i].before.convertToFormat(QImage::Format_ARGB32);
        if (previous.isNull() || next.isNull()) return fail("allocation_failed");
        for (int y = 0; y < height; ++y) {
            const auto* a = reinterpret_cast<const QRgb*>(previous.constScanLine(y));
            const auto* b = reinterpret_cast<const QRgb*>(next.constScanLine(y));
            if (std::memcmp(a, b, size_t(width) * kBytesPerPixel) == 0) continue;
            for (int x = 0; x < width; ++x) {
                if (a[x] != b[x]) {
                    auto& top = requiredTop[y * width + x];
                    top = std::max(top, y + 1);
                    seamChanged = seamChanged || region.contains(x, y);
                }
            }
        }
    }
    if (seamChanged) {
        region = largestCommonRegion(requiredTop, size);
        if (region.isEmpty()) return fail("discontinuous_capture");
    }
    if (omitsMovingColumns(pairs, region, width)) {
        return fail("moving_content_outside_crop");
    }
    // Keep the initial page header/title at its full width. Cropping that to
    // the later body column can cut text spanning above the sidebar. Only the
    // newly revealed content is appended; excluded columns remain transparent.
    const int prefixHeight = region.bottom() + 1;
    const qint64 outputHeight = qint64(prefixHeight) + totalShift;
    if (outputHeight > std::numeric_limits<int>::max()
        || outputHeight * width > maxOutputPixels) {
        return fail("pixel_budget_exceeded");
    }
    QImage previous;
    // Independently demand unique row evidence and exact cropped overlap.
    // Also reject content changes between the last settled and next start frame.
    for (const auto& pair : pairs) {
        const auto before = pair.before.copy(region).convertToFormat(QImage::Format_ARGB32);
        const auto after = pair.after.copy(region).convertToFormat(QImage::Format_ARGB32);
        if (before.isNull() || after.isNull()) return fail("allocation_failed");
        if (!previous.isNull() && previous != before) return fail("discontinuous_capture");
        ShiftEstimatorOptions options;
        options.expectedShift = pair.shift;
        options.searchRadius = 0;
        options.minMatchRatio = 1.0;
        const auto estimate = estimateVerticalShift(computeRowHashes(before), computeRowHashes(after), options);
        if (estimate.status != ShiftEstimate::Status::Found || estimate.shift != pair.shift) {
            return fail("insufficient_content_evidence");
        }
        previous = after;
    }
    ScrollContentResult result;
    result.contentRect = region;
    result.image = QImage(width, int(outputHeight), QImage::Format_ARGB32);
    if (result.image.isNull()) return fail("allocation_failed");
    result.image.fill(Qt::transparent);
    int destinationY = 0;
    const auto append = [&](const QImage& source, int top, int rows, int left, int columns) {
        const auto image = source.convertToFormat(QImage::Format_ARGB32);
        if (image.isNull()) return false;
        for (int y = top; y < top + rows; ++y) {
            std::memcpy(result.image.scanLine(destinationY++) + qsizetype(left) * kBytesPerPixel,
                        image.constScanLine(y) + qsizetype(left) * kBytesPerPixel,
                        size_t(columns) * kBytesPerPixel);
        }
        return true;
    };
    if (!append(pairs.first().before, 0, prefixHeight, 0, width)) return fail("allocation_failed");
    for (const auto& pair : pairs) {
        if (!append(pair.after, region.bottom() + 1 - pair.shift, pair.shift, region.x(), region.width())) {
            return fail("allocation_failed");
        }
    }
    return result;
}

} // namespace

ScrollContentResult assembleScrollContent(const QVector<ScrollContentPair>& pairs, qint64 maxOutputPixels)
{
    try {
        return assemble(pairs, maxOutputPixels);
    } catch (const std::bad_alloc&) {
        return fail("allocation_failed");
    }
}

} // namespace snaptray::scrollcapture
