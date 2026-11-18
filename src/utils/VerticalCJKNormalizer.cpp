#include "utils/VerticalCJKNormalizer.h"

#include "utils/OCRImageUtils.h"

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace VerticalCJKNormalizer {
namespace {

// Ink must differ from the background by at least this much (0-255 luminance).
constexpr int kMinContrast = 24;
constexpr int kMinForegroundThreshold = 12;
// Foreground threshold as a fraction of the strongest reliable ink contrast,
// so low-contrast text still separates while anti-aliasing halos do not.
constexpr double kForegroundContrastFraction = 0.4;
// Ignore the brightest few outliers (noise) when measuring ink contrast.
constexpr int kContrastOutlierDivisor = 5000;
constexpr int kMinContrastPixels = 3;

constexpr int kMinColumnWidth = 10;
// A single vertical column is clearly taller than it is wide.
constexpr double kMinTallAspect = 1.35;
// An empty vertical strip this wide (of the ink width) means separate columns.
constexpr double kColumnGapFraction = 0.1;
constexpr int kMinColumnGapPixels = 3;

// Upright CJK glyphs occupy roughly square cells whose side matches the column
// width. Detached strokes of one glyph (三, 六, 川) therefore never span more
// than about one column width, which is the merge limit for row bands.
constexpr double kMaxMergedGlyphFraction = 1.05;
constexpr double kGlyphLikeMinFraction = 0.5;
constexpr double kGlyphLikeMaxFraction = 1.15;
// Anything taller than this is not one glyph (e.g. a rotated Latin word).
constexpr double kMaxBandFraction = 1.3;
constexpr double kRequiredGlyphLikeShare = 0.65;
// A thin row run split into many horizontal pieces is a line of letters (UI
// list items, code), not a CJK stroke. Glyph strokes (三, 二, 六's dot) are
// solid or split into at most a few pieces.
constexpr double kTextLineMaxHeightFraction = 0.35;
constexpr int kTextLineMinSegments = 4;
// Latin letters turned sideways sit on a shared baseline and x-height line,
// so many row runs end at the same interior column position. Upright CJK
// glyphs either fill the column or end at glyph-specific positions.
constexpr double kSharedEdgeMinOffsetFraction = 0.15;
constexpr double kSharedEdgeToleranceFraction = 0.04;
constexpr double kSharedEdgeMinShare = 0.4;
constexpr double kSharedEdgeMinRunFraction = 0.25;

constexpr int kCropBleedPixels = 2;
constexpr int kMinGlyphGap = 4;
constexpr int kGlyphGapDivisor = 4;
constexpr int kMinOuterMargin = 8;
constexpr int kOuterMarginDivisor = 3;
// Small glyphs are upscaled so the OCR engine sees a readable text height.
constexpr int kTargetGlyphSize = 48;
constexpr int kMaxUpscale = 4;

struct Band {
    int start = 0; // inclusive row
    int end = 0;   // inclusive row
    int height() const { return end - start + 1; }
};

QImage opaqueRgb(const QImage& source)
{
    QImage fitted = OCRImageUtils::fitForRecognition(source, kMaxSourceDimension);
    if (fitted.isNull()) {
        return {};
    }
    if (!fitted.hasAlphaChannel()) {
        return fitted.convertToFormat(QImage::Format_RGB32);
    }
    // Transparent pixels would otherwise become black and look like ink.
    QImage opaque(fitted.size(), QImage::Format_RGB32);
    opaque.fill(Qt::white);
    QPainter painter(&opaque);
    painter.drawImage(0, 0, fitted);
    return opaque;
}

int medianOf(std::vector<int>& values)
{
    auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

QRgb estimateBackground(const QImage& image)
{
    std::vector<int> reds;
    std::vector<int> greens;
    std::vector<int> blues;
    const int w = image.width();
    const int h = image.height();
    auto sample = [&](int x, int y) {
        const QRgb pixel = image.pixel(x, y);
        reds.push_back(qRed(pixel));
        greens.push_back(qGreen(pixel));
        blues.push_back(qBlue(pixel));
    };
    for (int x = 0; x < w; ++x) {
        sample(x, 0);
        sample(x, h - 1);
    }
    for (int y = 1; y < h - 1; ++y) {
        sample(0, y);
        sample(w - 1, y);
    }
    return qRgb(medianOf(reds), medianOf(greens), medianOf(blues));
}

// Returns the per-pixel foreground mask, or an empty vector when no ink stands
// out from the background.
std::vector<unsigned char> foregroundMask(const QImage& image, QRgb background)
{
    const int w = image.width();
    const int h = image.height();
    const int backgroundGray = qGray(background);
    std::vector<unsigned char> diffs(static_cast<size_t>(w) * h);
    std::array<int, 256> histogram{};
    for (int y = 0; y < h; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int diff = std::abs(qGray(line[x]) - backgroundGray);
            diffs[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(diff);
            ++histogram[diff];
        }
    }

    const int minPixels = std::max(kMinContrastPixels, (w * h) / kContrastOutlierDivisor);
    int peak = 0;
    int accumulated = 0;
    for (int d = 255; d > 0; --d) {
        accumulated += histogram[d];
        if (accumulated >= minPixels) {
            peak = d;
            break;
        }
    }
    if (peak < kMinContrast) {
        return {};
    }

    const int threshold = std::max(kMinForegroundThreshold,
                                   static_cast<int>(std::lround(peak * kForegroundContrastFraction)));
    std::vector<unsigned char> mask(diffs.size());
    for (size_t i = 0; i < diffs.size(); ++i) {
        mask[i] = diffs[i] >= threshold ? 1 : 0;
    }
    return mask;
}

int horizontalSegments(const std::vector<unsigned char>& mask, int imageWidth,
                       const Band& run, int left, int right)
{
    int segments = 0;
    bool inInk = false;
    for (int x = left; x <= right; ++x) {
        bool ink = false;
        for (int y = run.start; y <= run.end && !ink; ++y) {
            ink = mask[static_cast<size_t>(y) * imageWidth + x] != 0;
        }
        if (ink && !inInk) {
            ++segments;
        }
        inInk = ink;
    }
    return segments;
}

// True when many runs stop at the same interior position on one side.
bool hasSharedInteriorEdge(const std::vector<unsigned char>& mask, int imageWidth,
                           const std::vector<Band>& runs, int left, int right)
{
    const int columnWidth = right - left + 1;
    std::vector<int> leftOffsets;
    std::vector<int> rightOffsets;
    for (const Band& run : runs) {
        int minX = right;
        int maxX = left;
        for (int y = run.start; y <= run.end; ++y) {
            const unsigned char* row = mask.data() + static_cast<size_t>(y) * imageWidth;
            for (int x = left; x <= right; ++x) {
                if (row[x]) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                }
            }
        }
        leftOffsets.push_back(minX - left);
        rightOffsets.push_back(right - maxX);
    }
    const int minOffset = static_cast<int>(std::ceil(columnWidth * kSharedEdgeMinOffsetFraction));
    const int tolerance = std::max(1, static_cast<int>(std::lround(columnWidth * kSharedEdgeToleranceFraction)));
    const auto required = static_cast<std::ptrdiff_t>(std::ceil(runs.size() * kSharedEdgeMinShare));
    const double minLetterHeight = columnWidth * kSharedEdgeMinRunFraction;
    // Only letter-sized runs count: thin CJK strokes (三, 二) can share edges
    // by symmetry, while a sideways letter is as tall as its advance width.
    const auto shared = [&](const std::vector<int>& offsets) {
        for (size_t c = 0; c < offsets.size(); ++c) {
            if (offsets[c] < minOffset || runs[c].height() < minLetterHeight) {
                continue;
            }
            std::ptrdiff_t near = 0;
            for (size_t i = 0; i < offsets.size(); ++i) {
                if (runs[i].height() >= minLetterHeight && std::abs(offsets[i] - offsets[c]) <= tolerance) {
                    ++near;
                }
            }
            if (near >= required) {
                return true;
            }
        }
        return false;
    };
    return shared(leftOffsets) || shared(rightOffsets);
}

std::vector<Band> mergeDetachedStrokes(const std::vector<Band>& runs, int columnWidth)
{
    const double maxMergedHeight = columnWidth * kMaxMergedGlyphFraction;
    std::vector<Band> bands;
    for (const Band& run : runs) {
        if (!bands.empty() && run.end - bands.back().start + 1 <= maxMergedHeight) {
            bands.back().end = run.end;
        } else {
            bands.push_back(run);
        }
    }
    return bands;
}

} // namespace

Result normalize(const QImage& source)
{
    Result result;
    const QImage image = opaqueRgb(source);
    if (image.isNull() || image.width() < 2 || image.height() < 2) {
        return result;
    }

    const QRgb background = estimateBackground(image);
    const std::vector<unsigned char> mask = foregroundMask(image, background);
    if (mask.empty()) {
        result.rejection = Rejection::NoForeground;
        return result;
    }

    const int w = image.width();
    const int h = image.height();
    std::vector<int> rowInk(h, 0);
    std::vector<int> columnInk(w, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (mask[static_cast<size_t>(y) * w + x]) {
                ++rowInk[y];
                ++columnInk[x];
            }
        }
    }
    const auto firstInk = [](const std::vector<int>& ink) {
        return static_cast<int>(std::find_if(ink.begin(), ink.end(), [](int c) { return c > 0; }) - ink.begin());
    };
    const auto lastInk = [](const std::vector<int>& ink) {
        return static_cast<int>(ink.rend() - std::find_if(ink.rbegin(), ink.rend(), [](int c) { return c > 0; })) - 1;
    };
    const int left = firstInk(columnInk);
    const int right = lastInk(columnInk);
    const int top = firstInk(rowInk);
    const int bottom = lastInk(rowInk);
    if (left > right || top > bottom) {
        result.rejection = Rejection::NoForeground;
        return result;
    }
    const int columnWidth = right - left + 1;
    const int inkHeight = bottom - top + 1;

    const int maxColumnGap = std::max(kMinColumnGapPixels,
                                      static_cast<int>(std::ceil(columnWidth * kColumnGapFraction)));
    int gap = 0;
    for (int x = left; x <= right; ++x) {
        gap = columnInk[x] == 0 ? gap + 1 : 0;
        if (gap >= maxColumnGap) {
            result.rejection = Rejection::MultipleColumns;
            return result;
        }
    }

    if (columnWidth < kMinColumnWidth || inkHeight < columnWidth * kMinTallAspect) {
        result.rejection = Rejection::NotTallColumn;
        return result;
    }

    std::vector<Band> runs;
    for (int y = top; y <= bottom; ++y) {
        if (rowInk[y] == 0) {
            continue;
        }
        if (!runs.empty() && runs.back().end == y - 1) {
            runs.back().end = y;
        } else {
            runs.push_back({y, y});
        }
    }
    int textLineRuns = 0;
    for (const Band& run : runs) {
        if (run.height() <= columnWidth * kTextLineMaxHeightFraction
            && horizontalSegments(mask, w, run, left, right) >= kTextLineMinSegments) {
            ++textLineRuns;
        }
    }
    if (textLineRuns * 2 >= static_cast<int>(runs.size())) {
        result.rejection = Rejection::NotGlyphLike;
        return result;
    }

    const std::vector<Band> bands = mergeDetachedStrokes(runs, columnWidth);
    const int glyphCount = static_cast<int>(bands.size());
    if (glyphCount < kMinGlyphCount || glyphCount > kMaxGlyphCount) {
        result.rejection = Rejection::GlyphCountOutOfRange;
        return result;
    }

    int glyphLike = 0;
    int tallestBand = 0;
    for (const Band& band : bands) {
        const double fraction = static_cast<double>(band.height()) / columnWidth;
        if (fraction > kMaxBandFraction) {
            result.rejection = Rejection::NotGlyphLike;
            return result;
        }
        if (fraction >= kGlyphLikeMinFraction && fraction <= kGlyphLikeMaxFraction) {
            ++glyphLike;
        }
        tallestBand = std::max(tallestBand, band.height());
    }
    if (glyphLike < static_cast<int>(std::ceil(glyphCount * kRequiredGlyphLikeShare))
        || hasSharedInteriorEdge(mask, w, runs, left, right)) {
        result.rejection = Rejection::NotGlyphLike;
        return result;
    }

    // Lay glyphs left to right in reading order, centered on one baseline row.
    const int cropLeft = std::max(0, left - kCropBleedPixels);
    const int cropRight = std::min(w - 1, right + kCropBleedPixels);
    const int cropWidth = cropRight - cropLeft + 1;
    const int glyphGap = std::max(kMinGlyphGap, columnWidth / kGlyphGapDivisor);
    const int margin = std::max(kMinOuterMargin, columnWidth / kOuterMarginDivisor);
    const int cellHeight = tallestBand + 2 * kCropBleedPixels;
    const int canvasWidth = 2 * margin + glyphCount * cropWidth + (glyphCount - 1) * glyphGap;
    const int canvasHeight = 2 * margin + cellHeight;

    QImage canvas(canvasWidth, canvasHeight, QImage::Format_RGB32);
    canvas.fill(background);
    {
        QPainter painter(&canvas);
        int x = margin;
        for (int i = 0; i < glyphCount; ++i) {
            const Band& band = bands[i];
            // Bleed into the gap for anti-aliased edges, but never past the
            // midpoint to a neighbouring glyph.
            const int prevEnd = i > 0 ? bands[i - 1].end : -1;
            const int nextStart = i + 1 < glyphCount ? bands[i + 1].start : h;
            const int cropTop = std::max({0, band.start - kCropBleedPixels, (prevEnd + band.start) / 2 + 1});
            const int cropBottom = std::min({h - 1, band.end + kCropBleedPixels, (band.end + nextStart) / 2});
            const QRect crop(cropLeft, cropTop, cropWidth, cropBottom - cropTop + 1);
            const int y = margin + (cellHeight - crop.height()) / 2;
            painter.drawImage(QPoint(x, y), image, crop);
            x += cropWidth + glyphGap;
        }
    }

    if (columnWidth < kTargetGlyphSize) {
        const int scale = std::min(kMaxUpscale, (kTargetGlyphSize + columnWidth - 1) / columnWidth);
        if (scale > 1) {
            canvas = canvas.scaled(canvas.size() * scale, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }

    result.image = canvas;
    result.glyphCount = glyphCount;
    result.rejection = Rejection::None;
    return result;
}

} // namespace VerticalCJKNormalizer
