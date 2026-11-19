#ifndef MEASUREEDGESCANNER_H
#define MEASUREEDGESCANNER_H

#include <QImage>
#include <QPoint>
#include <optional>

/**
 * @brief Finds the visible edges around a pixel for the Measure tool (#348).
 *
 * Pure image code: no widgets, no screens. It works on device pixels of an
 * image in Format_ARGB32 / Format_RGB32 / Format_ARGB32_Premultiplied and
 * scans one row or column at a time, so a hover costs at most width + height
 * pixel reads.
 *
 * What counts as an edge:
 * - Each pixel is compared with the mean of the last few pixels that matched,
 *   not with the starting pixel, so slow drifts (gradients) never read as an
 *   edge.
 * - The threshold grows with the noise measured around the starting pixel
 *   along the scan direction, so wallpaper and compression noise (and a steep
 *   gradient) are ignored while flat UI still yields low-contrast edges.
 * - A deviation is examined as a whole run: only a step clearly sharper than
 *   the rest of its run is an edge. A soft ramp (shadow, vignette) is skipped
 *   and scanning resumes where it ends, so the element casting a shadow is
 *   still found.
 * - An anti-aliased edge pixel belongs to whichever side covers most of it.
 *
 * The result is an estimate from pixels, not a layout or design-file size.
 * The approach follows the one described in Snapzy's Screen Ruler
 * (BSD-3-Clause); this is an independent implementation.
 */
namespace MeasureEdgeScanner {

/// Inclusive pixel range along one axis that contains the origin.
struct Span {
    int first = 0;
    int last = 0;
    /// False when the scan reached the image border without an edge.
    bool firstIsEdge = false;
    bool lastIsEdge = false;

    int length() const { return last - first + 1; }
    /// Both ends are found edges. An image border is where the capture was
    /// cut, not part of the content, so a span reaching it is no measurement.
    bool isReliable() const { return firstIsEdge && lastIsEdge; }
};

/// The span along @p orientation through @p origin, which is clamped into
/// the image. Returns nothing for an empty or unsupported image.
std::optional<Span> scan(const QImage& image, const QPoint& origin, Qt::Orientation orientation);

/// Closest detected pixel boundary within @p radius of @p origin along the axis.
/// Pixel boundaries are half-open (the right edge of pixel x is at x + 1).
std::optional<int> nearbyEdge(const QImage& image, const QPointF& origin,
                              Qt::Orientation orientation, qreal radius);

/// Trimmed mean of the largest-channel difference between pixels next to each
/// other along @p orientation around @p origin: 0 on flat UI, a few levels on
/// noisy content.
double noiseLevel(const QImage& image, const QPoint& origin, Qt::Orientation orientation);

/// Whether @p image can be scanned without conversion.
bool isSupportedFormat(QImage::Format format);

} // namespace MeasureEdgeScanner

#endif // MEASUREEDGESCANNER_H
