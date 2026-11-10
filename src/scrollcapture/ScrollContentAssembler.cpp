#include "scrollcapture/ScrollContentAssembler.h"
#include "scrollcapture/FrameRowHashes.h"
#include "scrollcapture/RowHashShiftEstimator.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace snaptray::scrollcapture {

ScrollContentResult assembleScrollContent(const QVector<ScrollContentPair>& pairs,
                                         qint64 maxOutputPixels)
{
    const auto fail = [](const char* reason) {
        ScrollContentResult result;
        result.error = QLatin1String(reason);
        return result;
    };
    if (pairs.isEmpty() || pairs.first().before.isNull() || maxOutputPixels <= 0) {
        return fail("invalid_input");
    }
    const QSize size = pairs.first().before.size();
    const int width = size.width();
    const int height = size.height();
    if (qint64(width) * height > std::min<qint64>(maxOutputPixels, std::numeric_limits<int>::max())) {
        return fail("pixel_budget_exceeded");
    }
    int minShift = height;
    int maxShift = 0;
    qint64 totalShift = 0;
    for (const auto& pair : pairs) {
        if (pair.before.size() != size || pair.after.size() != size
            || pair.shift <= 0 || pair.shift >= height / 2) {
            return fail("invalid_geometry_or_shift");
        }
        minShift = std::min(minShift, pair.shift);
        maxShift = std::max(maxShift, pair.shift);
        totalShift += pair.shift;
    }
    const int overlapHeight = height - maxShift;
    // A pixel is blocked if ANY pair disagrees with the supplied translation.
    // Static textured regions fail this check; uniform blank margins may pass.
    QVector<quint8> blocked(width * overlapHeight, 0);
    for (const auto& pair : pairs) {
        const auto before = pair.before.convertToFormat(QImage::Format_ARGB32);
        const auto after = pair.after.convertToFormat(QImage::Format_ARGB32);
        if (before.isNull() || after.isNull()) return fail("allocation_failed");
        for (int y = 0; y < overlapHeight; ++y) {
            const auto* a = reinterpret_cast<const QRgb*>(before.constScanLine(y + pair.shift));
            const auto* b = reinterpret_cast<const QRgb*>(after.constScanLine(y));
            for (int x = 0; x < width; ++x) {
                blocked[y * width + x] |= a[x] != b[x];
            }
        }
    }
    // Largest empty rectangle via histogram stacks, O(width * height).
    // Require a majority of BOTH viewport dimensions, not a lucky tiny patch.
    QRect region;
    qint64 bestArea = 0;
    QVector<int> heights(width + 1, 0);
    QVector<int> stack;
    for (int y = 0; y < overlapHeight; ++y) {
        for (int x = 0; x < width; ++x) {
            heights[x] = blocked[y * width + x] ? 0 : heights[x] + 1;
        }
        stack.clear();
        for (int x = 0; x <= width; ++x) {
            while (!stack.isEmpty() && heights[stack.last()] > heights[x]) {
                const int h = heights[stack.takeLast()];
                const int left = stack.isEmpty() ? 0 : stack.last() + 1;
                const int w = x - left;
                const int cropHeight = h + minShift;
                const qint64 area = qint64(w) * cropHeight;
                if (w > width / 2 && cropHeight > height / 2 && area > bestArea) {
                    region = QRect(left, y - h + 1, w, cropHeight);
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
    result.viewport = region;
    result.image = QImage(width, int(outputHeight), QImage::Format_ARGB32);
    if (result.image.isNull()) return fail("allocation_failed");
    result.image.fill(Qt::transparent);
    int destinationY = 0;
    const auto append = [&](const QImage& source, int top, int rows, int left, int columns) {
        const auto image = source.convertToFormat(QImage::Format_ARGB32);
        for (int y = top; y < top + rows; ++y) {
            std::memcpy(result.image.scanLine(destinationY++) + left * 4, image.constScanLine(y) + left * 4,
                        size_t(columns) * 4);
        }
    };
    append(pairs.first().before, 0, prefixHeight, 0, width);
    for (const auto& pair : pairs) {
        append(pair.after, region.bottom() + 1 - pair.shift, pair.shift, region.x(), region.width());
    }
    return result;
}

} // namespace snaptray::scrollcapture
