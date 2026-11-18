#ifndef IMAGESTRIPTRANSFORM_H
#define IMAGESTRIPTRANSFORM_H

#include <QImage>
#include <QRectF>
#include <QSize>

/**
 * @brief Removes a full-width or full-height strip from an image, or inserts
 * a transparent one (#371).
 *
 * Unlike a crop, which keeps one rectangle, this joins what lies on both
 * sides of the strip. Everything is in device pixels of the image; nothing
 * here knows about zoom, logical sizes or annotations beyond the small
 * placement helpers at the end. The functions are pure: a failure returns an
 * error and leaves the input as it was.
 */
namespace ImageStrip {

enum class Axis {
    Rows = 0,   // a horizontal band: rows are removed or inserted
    Columns,    // a vertical band: columns are removed or inserted
};

enum class Operation {
    Remove = 0,
    Insert,
};

struct Request {
    Axis axis = Axis::Rows;
    Operation operation = Operation::Remove;
    // Remove: first row/column of the strip. Insert: the row/column the blank
    // strip is placed before (the image extent appends it at the end).
    int start = 0;
    int length = 0;
};

enum class Error {
    None = 0,
    EmptyImage,
    InvalidStrip,   // empty, negative or outside the image
    WholeImage,     // removing it would leave nothing
    TooLarge,       // the result would exceed the size limits below
    OutOfMemory,
};

// Limits of a result. They match what the merged-pin layout accepts.
inline constexpr int kMaxDimension = 32767;
inline constexpr qint64 kMaxPixels = 64ll * 1000 * 1000;

struct Result {
    QImage image;
    Error error = Error::None;
    bool ok() const { return error == Error::None && !image.isNull(); }
};

int extentAlong(const QSize& size, Axis axis);
// Longest strip that may still be inserted into an image of `size`.
int maxInsertLength(const QSize& size, Axis axis);
Error validate(const QSize& imageSize, const Request& request);
// Size of the result; an invalid size when the request is not valid.
QSize resultSize(const QSize& imageSize, const Request& request);
Result apply(const QImage& image, const Request& request);

// ---- geometry of things drawn on the image ----

enum class Placement {
    Before = 0,   // entirely on the near side: stays where it is
    After,        // entirely on the far side: shifts with the pixels
    Crossing,     // touches the strip (or spans the insertion line)
};

// Where `bounds` lies relative to a strip given in the same coordinates
// (start and length along the axis; they may be fractional in a zoomed view).
Placement placementOf(const QRectF& bounds, Axis axis, Operation operation,
                      qreal start, qreal length);
// Smallest strip length, in source pixels, that is a whole number of logical
// pixels (`pixelsPerLogical` is the device pixel ratio) and also a whole
// number of view units (`viewPerPixel` is how many annotation/view units one
// source pixel covers). Only multiples of it keep annotations exactly on
// their pixels: anything else leaves a rounding error that grows with every
// strip and can slide a redaction off what it hides. 0 when no length up to
// kMaxLengthStep qualifies, for example at an odd zoom level.
inline constexpr int kMaxLengthStep = 64;
int lengthStep(qreal pixelsPerLogical, qreal viewPerPixel);

// The shift of something placed After, along the axis: negative for a
// removal, positive for an insertion.
QPointF shiftAfter(Axis axis, Operation operation, qreal length);

} // namespace ImageStrip

#endif // IMAGESTRIPTRANSFORM_H
