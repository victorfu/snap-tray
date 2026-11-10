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
    if (region.isEmpty()) return fail("no_common_content_region");
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
