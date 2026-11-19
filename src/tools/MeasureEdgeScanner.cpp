#include "tools/MeasureEdgeScanner.h"

#include <QtGlobal>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace MeasureEdgeScanner {
namespace {

// On noise-free UI a step must exceed this (0-255, largest channel) to be an
// edge, so a difference of 4 levels is the smallest one found.
constexpr int kMinThreshold = 3;
// The threshold is at least kNoiseFactor x noise + 1.
constexpr double kNoiseFactor = 3.0;
// From this noise level a single deviating pixel needs the next one to agree,
// and samples average three pixels across the scan direction.
constexpr double kNoisyLevel = 0.5;
// On noisy content, a deviation up to this many thresholds may be a lone
// noisy pixel and needs confirmation.
constexpr double kLoneNoiseThresholds = 2.0;
// Share of the smallest neighbor differences kept for the noise level; the
// rest are edges (a hairline next to the origin) and are dropped.
constexpr double kNoiseKeptShare = 0.7;
constexpr int kNoiseRadius = 4;
// Pixels averaged into the running reference.
constexpr int kReferenceLength = 4;
// Longest transition examined for its sharpest step.
constexpr int kMaxTransitionLength = 64;
// An edge's step must be this many times larger than the steps around it.
constexpr int kSharpnessRatio = 3;
// At least this share of an anti-aliased pixel must look like the scanned
// side for the pixel to count on that side.
constexpr double kCoverageForScannedSide = 0.5;

constexpr int kChannels = 4;
using Pixel = std::array<int, kChannels>;

Pixel unpack(QRgb rgb)
{
    return {qRed(rgb), qGreen(rgb), qBlue(rgb), qAlpha(rgb)};
}

int largestChannelDelta(QRgb a, QRgb b)
{
    const Pixel pa = unpack(a);
    const Pixel pb = unpack(b);
    int delta = 0;
    for (int c = 0; c < kChannels; ++c) {
        delta = std::max(delta, std::abs(pa[c] - pb[c]));
    }
    return delta;
}

QRgb pixelAt(const QImage& image, int x, int y)
{
    return reinterpret_cast<const QRgb*>(image.constScanLine(y))[x];
}

double trimmedMean(std::vector<int>& values)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto kept = std::max<size_t>(
        1, static_cast<size_t>(static_cast<double>(values.size()) * kNoiseKeptShare));
    long long sum = 0;
    for (size_t i = 0; i < kept; ++i) {
        sum += values[i];
    }
    return static_cast<double>(sum) / static_cast<double>(kept);
}

// Running mean of the last kReferenceLength matched pixels.
class Reference {
public:
    void reset(const Pixel& p)
    {
        m_count = 0;
        m_next = 0;
        m_sum.fill(0);
        add(p);
    }

    void add(const Pixel& p)
    {
        if (m_count == kReferenceLength) {
            for (int c = 0; c < kChannels; ++c) {
                m_sum[c] -= m_ring[m_next][c];
            }
        } else {
            ++m_count;
        }
        m_ring[m_next] = p;
        for (int c = 0; c < kChannels; ++c) {
            m_sum[c] += p[c];
        }
        m_next = (m_next + 1) % kReferenceLength;
    }

    // Largest channel distance from the mean, and its channel.
    std::pair<double, int> deviation(const Pixel& p) const
    {
        double best = -1.0;
        int channel = 0;
        for (int c = 0; c < kChannels; ++c) {
            const double d = std::abs(static_cast<double>(p[c]) - static_cast<double>(m_sum[c]) / m_count);
            if (d > best) {
                best = d;
                channel = c;
            }
        }
        return {best, channel};
    }

private:
    std::array<Pixel, kReferenceLength> m_ring{};
    Pixel m_sum{};
    int m_count = 0;
    int m_next = 0;
};

struct Step {
    int index;   // the pixel that differs from the one before it
    int size;    // absolute difference in the examined channel
};

// Scans one line from an origin pixel in steps of (dx, dy).
class LineScanner {
public:
    LineScanner(const QImage& image, QPoint origin, QPoint step, int count, int threshold, bool noisy)
        : m_image(image), m_origin(origin), m_step(step), m_count(count),
          m_threshold(threshold), m_noisy(noisy)
    {
    }

    // Steps from the origin to the first pixel past the edge, or nothing when
    // the line has none. The origin itself is index 0 and always matches.
    std::optional<int> edgeIndex() const
    {
        if (m_count < 2) {
            return std::nullopt;
        }
        Reference reference;
        reference.reset(sample(0));
        int i = 1;
        while (i < m_count) {
            const Pixel p = sample(i);
            const auto [deviation, channel] = reference.deviation(p);
            if (deviation <= m_threshold || !confirmed(reference, i, deviation)) {
                reference.add(p);
                ++i;
                continue;
            }
            const Transition transition = examineTransition(i, channel);
            if (transition.edge) {
                return transition.edge;
            }
            // A soft ramp: carry on from where it ended.
            i = std::max(transition.end, i + 1);
            if (i < m_count) {
                reference.reset(sample(i - 1));
            }
        }
        return std::nullopt;
    }

private:
    Pixel sample(int i) const
    {
        const int x = m_origin.x() + i * m_step.x();
        const int y = m_origin.y() + i * m_step.y();
        const Pixel center = unpack(pixelAt(m_image, x, y));
        if (!m_noisy) {
            return center;
        }
        // Average across the scan direction: an edge crossing the line stays
        // sharp while noise drops.
        const int px = m_step.y();
        const int py = m_step.x();
        const int maxX = m_image.width() - 1;
        const int maxY = m_image.height() - 1;
        const Pixel a = unpack(pixelAt(m_image, qBound(0, x - px, maxX), qBound(0, y - py, maxY)));
        const Pixel b = unpack(pixelAt(m_image, qBound(0, x + px, maxX), qBound(0, y + py, maxY)));
        Pixel out{};
        for (int c = 0; c < kChannels; ++c) {
            out[c] = (a[c] + center[c] + b[c] + 1) / 3;
        }
        return out;
    }

    int channelValue(int i, int channel) const { return sample(i)[channel]; }

    // On noisy content a lone pixel just over the threshold only starts a
    // transition when the next pixel deviates too.
    bool confirmed(const Reference& reference, int i, double deviation) const
    {
        if (!m_noisy || deviation > kLoneNoiseThresholds * m_threshold) {
            return true;
        }
        return i + 1 < m_count && reference.deviation(sample(i + 1)).first > m_threshold;
    }

    struct Transition {
        std::optional<int> edge;   // first pixel past the edge, if the run holds one
        int end = 0;               // where the run ended
    };

    // Walks the monotonic run starting at @p start in @p channel.
    Transition examineTransition(int start, int channel) const
    {
        const int direction = channelValue(start, channel) >= channelValue(start - 1, channel) ? 1 : -1;
        std::vector<Step> steps;
        int end = start;
        while (end < m_count && end - start < kMaxTransitionLength) {
            const int step = channelValue(end, channel) - channelValue(end - 1, channel);
            if (step * direction <= 0) {
                break;
            }
            steps.push_back({end, std::abs(step)});
            ++end;
        }

        // The sharpest window of one or two steps (anti-aliasing splits an
        // edge over two neighboring pixels).
        int best = -1;
        int bestStrength = 0;
        int bestWidth = 1;
        for (int k = 0; k < static_cast<int>(steps.size()); ++k) {
            const bool paired = k + 1 < static_cast<int>(steps.size());
            const int strength = steps[k].size + (paired ? steps[k + 1].size : 0);
            if (strength > bestStrength) {
                best = k;
                bestStrength = strength;
                bestWidth = paired ? 2 : 1;
            }
        }
        if (best >= 0 && bestStrength > m_threshold) {
            // Neighbors of a blurred ramp are nearly as large as its middle.
            int before = best > 0 ? steps[best - 1].size : 0;
            const QPoint precedingPixel = m_origin + (start - 2) * m_step;
            if (best == 0 && m_image.rect().contains(precedingPixel)) {
                // A ramp can already be moving when it crosses the threshold.
                // Count its preceding same-direction step in the sharpness check.
                before = std::max(0, direction * (channelValue(start - 1, channel)
                                                  - channelValue(start - 2, channel)));
            }
            const int afterIndex = best + bestWidth;
            const int after = afterIndex < static_cast<int>(steps.size()) ? steps[afterIndex].size : 0;
            if (bestStrength > kSharpnessRatio * std::max(before, after) * bestWidth) {
                return {placeEdge(steps[best].index, bestWidth == 2, channel), end};
            }
        }
        // A soft ramp. Scanning resumes where it ended, so a sharp step back
        // there (a card at the end of its shadow) is found as the next edge.
        return {std::nullopt, end};
    }

    // First pixel past the edge. With an anti-aliased pair, the partial pixel
    // stays on the scanned side when that side covers most of it.
    int placeEdge(int first, bool paired, int channel) const
    {
        if (!paired || first + 1 >= m_count) {
            return first;
        }
        const int before = channelValue(first - 1, channel);
        const int partial = channelValue(first, channel);
        const int settled = channelValue(first + 1, channel);
        const int span = settled - before;
        if (span == 0) {
            return first;
        }
        const double scannedSideCoverage = static_cast<double>(settled - partial) / span;
        return scannedSideCoverage >= kCoverageForScannedSide ? first + 1 : first;
    }

    const QImage& m_image;
    QPoint m_origin;
    QPoint m_step;
    int m_count;
    int m_threshold;
    bool m_noisy;
};

} // namespace

bool isSupportedFormat(QImage::Format format)
{
    return format == QImage::Format_ARGB32 || format == QImage::Format_RGB32
        || format == QImage::Format_ARGB32_Premultiplied;
}

double noiseLevel(const QImage& image, const QPoint& origin, Qt::Orientation orientation)
{
    if (image.isNull() || !isSupportedFormat(image.format())) {
        return 0.0;
    }
    const int x0 = qBound(0, origin.x(), image.width() - 1);
    const int y0 = qBound(0, origin.y(), image.height() - 1);
    const int left = std::max(0, x0 - kNoiseRadius);
    const int right = std::min(image.width() - 1, x0 + kNoiseRadius);
    const int top = std::max(0, y0 - kNoiseRadius);
    const int bottom = std::min(image.height() - 1, y0 + kNoiseRadius);

    const bool horizontal = orientation == Qt::Horizontal;
    std::vector<int> samples;
    samples.reserve(static_cast<size_t>((right - left + 1) * (bottom - top + 1)));
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (horizontal ? x == left : y == top) {
                continue;
            }
            const QRgb previous = horizontal ? pixelAt(image, x - 1, y) : pixelAt(image, x, y - 1);
            samples.push_back(largestChannelDelta(pixelAt(image, x, y), previous));
        }
    }
    return trimmedMean(samples);
}

namespace {

std::optional<Span> scanLine(const QImage& image, const QPoint& origin, Qt::Orientation orientation,
                             int searchExtent)
{
    if (image.isNull() || !isSupportedFormat(image.format())) {
        return std::nullopt;
    }
    const QPoint start(qBound(0, origin.x(), image.width() - 1),
                       qBound(0, origin.y(), image.height() - 1));
    const double noise = noiseLevel(image, start, orientation);
    const int noiseThreshold = noise > 0.0
        ? static_cast<int>(std::ceil(noise * kNoiseFactor)) + 1
        : 0;
    const int threshold = std::max(kMinThreshold, noiseThreshold);
    const bool noisy = noise >= kNoisyLevel;

    const bool horizontal = orientation == Qt::Horizontal;
    const int position = horizontal ? start.x() : start.y();
    const int extent = horizontal ? image.width() : image.height();
    const QPoint forward = horizontal ? QPoint(1, 0) : QPoint(0, 1);

    Span span;
    const std::optional<int> back =
        LineScanner(image, start, -forward, std::min(position + 1, searchExtent),
                    threshold, noisy).edgeIndex();
    span.firstIsEdge = back.has_value();
    span.first = back ? position - (*back - 1) : 0;

    const std::optional<int> ahead =
        LineScanner(image, start, forward, std::min(extent - position, searchExtent),
                    threshold, noisy).edgeIndex();
    span.lastIsEdge = ahead.has_value();
    span.last = ahead ? position + (*ahead - 1) : extent - 1;
    return span;
}

} // namespace

std::optional<Span> scan(const QImage& image, const QPoint& origin, Qt::Orientation orientation)
{
    return scanLine(image, origin, orientation, std::max(image.width(), image.height()));
}

std::optional<int> nearbyEdge(const QImage& image, const QPointF& origin,
                              Qt::Orientation orientation, qreal radius)
{
    const bool horizontal = orientation == Qt::Horizontal;
    const qreal position = horizontal ? origin.x() : origin.y();
    const qreal across = horizontal ? origin.y() : origin.x();
    const int extent = horizontal ? image.width() : image.height();
    const int acrossExtent = horizontal ? image.height() : image.width();
    if (extent == 0 || !qIsFinite(radius) ||
        !(radius > 0 && position >= -radius && position <= extent + radius
          && across >= 0 && across < acrossExtent)) {
        return std::nullopt;
    }
    // An active drag can go outside the pin. Start in the visible raster but
    // still measure the snap distance from the pointer's actual position.
    const int alongPixel = qFloor(qBound(0.0, position, qreal(extent - 1)));
    const int acrossPixel = qFloor(across);
    const QPoint pixel = horizontal ? QPoint(alongPixel, acrossPixel) : QPoint(acrossPixel, alongPixel);
    // Keep enough lookahead to distinguish a soft ramp from a sharp edge.
    const int searchExtent = static_cast<int>(std::min<qreal>(
        extent, std::ceil(radius) + kMaxTransitionLength + 1));
    const auto span = scanLine(image, pixel, orientation, searchExtent);
    if (!span) {
        return std::nullopt;
    }

    const qreal before = qAbs(position - span->first);
    const qreal after = qAbs(span->last + 1 - position);
    const bool canSnapBefore = span->firstIsEdge && before <= radius;
    const bool canSnapAfter = span->lastIsEdge && after <= radius;
    if (canSnapBefore && (!canSnapAfter || before < after)) {
        return span->first;
    }
    if (canSnapAfter && (!canSnapBefore || after < before)) {
        return span->last + 1;
    }
    return std::nullopt;
}

} // namespace MeasureEdgeScanner
