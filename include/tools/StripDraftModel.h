#ifndef STRIPDRAFTMODEL_H
#define STRIPDRAFTMODEL_H

#include "pinwindow/ImageStripTransform.h"

#include <QSize>
#include <QSizeF>

/**
 * @brief Uncommitted strip of the cut-out tool (#371).
 *
 * A band across the whole image, in source pixels along one axis: the rows or
 * columns to remove, or the blank band to insert before `start`. Positions
 * move pixel by pixel. Lengths are multiples of ImageStrip::lengthStep(), so
 * the pixels after the band, and every annotation on them, move by a whole
 * number of view units: no rounding error is left to add up.
 */
class StripDraftModel
{
public:
    using Axis = ImageStrip::Axis;
    using Operation = ImageStrip::Operation;

    enum class Part {
        None = 0,
        Body,       // inside the band: moves it
        StartEdge,  // the edge nearer the origin
        EndEdge,
    };

    // A different size discards the strip. `viewPerPixel` is how many view
    // (annotation) units one source pixel covers along each axis.
    void setSource(const QSize& pixelSize, qreal devicePixelRatio,
                   const QSizeF& viewPerPixel = QSizeF(1.0, 1.0));
    // Length unit along the current axis; 0 when no strip can be exact here.
    int lengthStep() const;
    QSize sourceSize() const { return m_sourceSize; }

    Axis axis() const { return m_axis; }
    // Changing the axis discards the strip: its numbers mean something else.
    void setAxis(Axis axis);
    Operation operation() const { return m_operation; }
    // Keeps the strip, re-fitted to what the new operation allows.
    void setOperation(Operation operation);

    bool hasStrip() const { return m_length > 0; }
    int start() const { return m_start; }
    int length() const { return m_length; }
    void clear() { m_start = 0; m_length = 0; }
    void restore(int start, int length);

    // The image extent along the axis, and the longest strip allowed now.
    int extent() const { return ImageStrip::extentAlong(m_sourceSize, m_axis); }
    int maxLength() const;

    // A new strip between two positions along the axis.
    void setFromDrag(int anchor, int current);
    void move(int startAtPress, int lengthAtPress, int delta);
    void nudge(int delta);
    void resize(int startAtPress, int lengthAtPress, Part edge, int current);

    // The request this strip stands for; valid only while hasStrip().
    ImageStrip::Request request() const;
    // Size of the image after applying; invalid without a valid strip.
    QSize resultSize() const { return ImageStrip::resultSize(m_sourceSize, request()); }

private:
    // Nearest multiple of lengthStep(), at least one step, at most `limit`;
    // 0 when not even one step fits.
    int legalLength(int pixels, int limit) const;
    void setClamped(int start, int length);

    QSize m_sourceSize;
    qreal m_dpr = 1.0;
    QSizeF m_viewPerPixel{1.0, 1.0};
    Axis m_axis = Axis::Rows;
    Operation m_operation = Operation::Remove;
    int m_start = 0;
    int m_length = 0;
};

#endif // STRIPDRAFTMODEL_H
