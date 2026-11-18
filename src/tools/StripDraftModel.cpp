#include "tools/StripDraftModel.h"

#include <QtMath>

void StripDraftModel::setSource(const QSize& pixelSize, qreal devicePixelRatio,
                                const QSizeF& viewPerPixel)
{
    const QSize size = pixelSize.isValid() ? pixelSize : QSize();
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    // Another zoom changes which lengths are exact, so the strip starts over.
    if (size != m_sourceSize || !qFuzzyCompare(dpr, m_dpr) || viewPerPixel != m_viewPerPixel) {
        clear();
    }
    m_sourceSize = size;
    m_dpr = dpr;
    m_viewPerPixel = viewPerPixel;
}

int StripDraftModel::lengthStep() const
{
    return ImageStrip::lengthStep(
        m_dpr, m_axis == Axis::Rows ? m_viewPerPixel.height() : m_viewPerPixel.width());
}

void StripDraftModel::setAxis(Axis axis)
{
    if (m_axis == axis) {
        return;
    }
    m_axis = axis;
    clear();
}

void StripDraftModel::setOperation(Operation operation)
{
    if (m_operation == operation) {
        return;
    }
    m_operation = operation;
    if (hasStrip()) {
        setClamped(m_start, m_length);
    }
}

void StripDraftModel::restore(int start, int length)
{
    if (length <= 0) {
        clear();
        return;
    }
    setClamped(start, length);
}

int StripDraftModel::maxLength() const
{
    const int imageExtent = extent();
    if (imageExtent <= 0) {
        return 0;
    }
    if (m_operation == Operation::Insert) {
        return ImageStrip::maxInsertLength(m_sourceSize, m_axis);
    }
    // Something must be left of the image.
    return imageExtent - 1;
}

int StripDraftModel::legalLength(int pixels, int limit) const
{
    const int step = lengthStep();
    if (limit <= 0 || step <= 0 || step > limit) {
        return 0;
    }
    const int steps = qBound(1, qRound(static_cast<qreal>(pixels) / step), limit / step);
    return steps * step;
}

void StripDraftModel::setClamped(int start, int length)
{
    const int imageExtent = extent();
    const int legal = legalLength(length, maxLength());
    if (imageExtent <= 0 || legal <= 0) {
        clear();
        return;
    }
    m_length = legal;
    // A band to remove lies inside the image; a blank one may start anywhere
    // up to its end, where it is appended.
    const int maxStart = m_operation == Operation::Remove ? imageExtent - legal : imageExtent;
    m_start = qBound(0, start, maxStart);
}

void StripDraftModel::setFromDrag(int anchor, int current)
{
    const int imageExtent = extent();
    if (imageExtent <= 0) {
        clear();
        return;
    }
    const int a = qBound(0, anchor, imageExtent);
    const int c = qBound(0, current, imageExtent);
    if (a == c) {
        clear();
        return;
    }
    const int length = legalLength(qAbs(c - a), maxLength());
    if (length <= 0) {
        clear();
        return;
    }
    // The anchor edge stays; the band grows toward the pointer.
    setClamped(c > a ? a : a - length, length);
}

void StripDraftModel::move(int startAtPress, int lengthAtPress, int delta)
{
    setClamped(startAtPress + delta, lengthAtPress);
}

void StripDraftModel::nudge(int delta)
{
    if (hasStrip()) {
        setClamped(m_start + delta, m_length);
    }
}

void StripDraftModel::resize(int startAtPress, int lengthAtPress, Part edge, int current)
{
    if (lengthAtPress <= 0 || (edge != Part::StartEdge && edge != Part::EndEdge)) {
        return;
    }
    const int imageExtent = extent();
    const int end = startAtPress + lengthAtPress;
    if (edge == Part::EndEdge) {
        // The start edge is fixed. An inserted band is not limited by the image.
        const int room = m_operation == Operation::Remove ? imageExtent - startAtPress : maxLength();
        const int length = legalLength(qMax(1, current - startAtPress), qMin(room, maxLength()));
        if (length > 0) {
            m_start = startAtPress;
            m_length = length;
        }
        return;
    }
    // The end edge is fixed and the start moves toward the origin or back.
    const int length = legalLength(qMax(1, end - current), qMin(end, maxLength()));
    if (length > 0) {
        m_start = end - length;
        m_length = length;
    }
}

ImageStrip::Request StripDraftModel::request() const
{
    ImageStrip::Request request;
    request.axis = m_axis;
    request.operation = m_operation;
    request.start = m_start;
    request.length = m_length;
    return request;
}
