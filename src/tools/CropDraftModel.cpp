#include "tools/CropDraftModel.h"

#include <QtMath>

namespace {

int boundInt(int low, int value, int high)
{
    return high < low ? low : qBound(low, value, high);
}

} // namespace

const QVector<CropDraftModel::AspectOption>& CropDraftModel::aspectOptions()
{
    static const QVector<AspectOption> kOptions = {
        {Aspect::Free, 0, 0},
        {Aspect::Original, 0, 0},
        {Aspect::Square, 1, 1},
        {Aspect::Wide16x9, 16, 9},
        {Aspect::Tall9x16, 9, 16},
        {Aspect::Standard4x3, 4, 3},
        {Aspect::Classic3x2, 3, 2},
    };
    return kOptions;
}

bool CropDraftModel::isValidAspect(int value)
{
    for (const AspectOption& option : aspectOptions()) {
        if (static_cast<int>(option.aspect) == value) {
            return true;
        }
    }
    return false;
}

void CropDraftModel::clear()
{
    m_rect = QRect();
    m_gestureActive = false;
    m_gestureStartRect = QRect();
}

int CropDraftModel::exactPositionStep(qreal viewPerPixel)
{
    if (viewPerPixel <= 0.0) {
        return 0;
    }
    constexpr qreal kTolerance = 1e-6;
    for (int step = 1; step <= kMaxPositionStep; ++step) {
        const qreal view = step * viewPerPixel;
        if (view > 0.5 && qAbs(view - qRound(view)) < kTolerance) {
            return step;
        }
    }
    return 0;
}

void CropDraftModel::setPositionStep(const QSize& step)
{
    const QSize bounded(qMax(1, step.width()), qMax(1, step.height()));
    if (bounded == m_positionStep) {
        return;
    }
    m_positionStep = bounded;
    if (hasRect()) {
        m_rect = snapped(m_rect);
    }
}

QRect CropDraftModel::snapped(const QRect& rect) const
{
    if (rect.isEmpty() || m_sourceSize.isEmpty()) {
        return rect;
    }
    const auto snapAxis = [](int pos, int extent, int imageExtent, int step) {
        const int maxPos = qMax(0, imageExtent - extent);
        if (step <= 1) {
            return qBound(0, pos, maxPos);
        }
        // Nearest grid line that still keeps the rect inside the image.
        const int lastLine = (maxPos / step) * step;
        return qBound(0, qRound(static_cast<qreal>(pos) / step) * step, lastLine);
    };
    return QRect(snapAxis(rect.x(), rect.width(), m_sourceSize.width(), m_positionStep.width()),
                 snapAxis(rect.y(), rect.height(), m_sourceSize.height(), m_positionStep.height()),
                 rect.width(), rect.height());
}

void CropDraftModel::beginGesture()
{
    m_gestureActive = true;
    m_gestureStartRect = m_rect;
}

void CropDraftModel::moveBy(const QPoint& delta)
{
    if (m_gestureActive && !m_gestureStartRect.isEmpty()) {
        moveRect(m_gestureStartRect, delta);
    }
}

void CropDraftModel::dragHandle(Handle handle, const QPoint& current)
{
    if (m_gestureActive && !m_gestureStartRect.isEmpty()) {
        resizeRect(m_gestureStartRect, handle, current);
    }
}

void CropDraftModel::endGesture()
{
    if (!m_gestureActive) {
        return;
    }
    if (!hasRect()) {
        m_rect = m_gestureStartRect;
    }
    m_gestureActive = false;
    m_gestureStartRect = QRect();
}

void CropDraftModel::cancelGesture()
{
    if (!m_gestureActive) {
        return;
    }
    m_rect = m_gestureStartRect;
    m_gestureActive = false;
    m_gestureStartRect = QRect();
}

void CropDraftModel::setSource(const QSize& pixelSize, qreal devicePixelRatio)
{
    const QSize size = pixelSize.isValid() ? pixelSize : QSize();
    if (size != m_sourceSize) {
        clear();
    }
    m_sourceSize = size;
    m_dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
}

void CropDraftModel::setViewTransposed(bool transposed)
{
    if (m_viewTransposed == transposed) {
        return;
    }
    m_viewTransposed = transposed;
    setAspect(m_aspect);
}

qreal CropDraftModel::sourceRatio() const
{
    if (m_aspect == Aspect::Free || m_sourceSize.isEmpty()) {
        return 0.0;
    }
    if (m_aspect == Aspect::Original) {
        return static_cast<qreal>(m_sourceSize.width()) / m_sourceSize.height();
    }
    for (const AspectOption& option : aspectOptions()) {
        if (option.aspect == m_aspect && option.widthUnits > 0 && option.heightUnits > 0) {
            const qreal ratio = static_cast<qreal>(option.widthUnits) / option.heightUnits;
            return m_viewTransposed ? 1.0 / ratio : ratio;
        }
    }
    return 0.0;
}

int CropDraftModel::legalExtent(int pixels, int maxPixels, Qt::Orientation axis) const
{
    if (maxPixels <= 0) {
        return 0;
    }
    // The limit is the whole image: it is shown as captured, whatever its size.
    const bool limitIsImage =
        maxPixels == (axis == Qt::Horizontal ? m_sourceSize.width() : m_sourceSize.height());
    if (limitIsImage && pixels >= maxPixels) {
        return maxPixels;
    }

    int steps = qMax(1, qRound(pixels / m_dpr));
    int extent = qRound(steps * m_dpr);
    while (extent > maxPixels && steps > 1) {
        --steps;
        extent = qRound(steps * m_dpr);
    }
    if (extent > maxPixels) {
        // Less room than one logical pixel: only the whole image fits.
        return limitIsImage ? maxPixels : 0;
    }
    return qMax(1, extent);
}

QSize CropDraftModel::fitSize(qreal width, qreal height, int maxWidth, int maxHeight) const
{
    if (maxWidth <= 0 || maxHeight <= 0) {
        return QSize(0, 0);
    }

    const qreal ratio = sourceRatio();
    if (ratio <= 0.0) {
        const int w = legalExtent(qRound(width), maxWidth, Qt::Horizontal);
        const int h = legalExtent(qRound(height), maxHeight, Qt::Vertical);
        return (w > 0 && h > 0) ? QSize(w, h) : QSize(0, 0);
    }

    // The axis the pointer moved further along drives the size.
    qreal w = qMax(width, height * ratio);
    qreal h = w / ratio;
    const qreal scale = qMin(1.0, qMin(maxWidth / w, maxHeight / h));
    w *= scale;

    int legalW = legalExtent(qRound(w), maxWidth, Qt::Horizontal);
    int legalH = legalW > 0 ? legalExtent(qRound(legalW / ratio), maxHeight, Qt::Vertical) : 0;
    if (legalW > 0 && legalH > 0 && legalW / ratio > maxHeight + 0.5) {
        // Rounding the width up left no room for its height: derive from height.
        legalW = legalExtent(qFloor(legalH * ratio), maxWidth, Qt::Horizontal);
    }
    return (legalW > 0 && legalH > 0) ? QSize(legalW, legalH) : QSize(0, 0);
}

void CropDraftModel::setRectClamped(const QRect& rect)
{
    if (rect.isEmpty() || m_sourceSize.isEmpty()) {
        return;
    }
    const int w = qMin(rect.width(), m_sourceSize.width());
    const int h = qMin(rect.height(), m_sourceSize.height());
    const int x = boundInt(0, rect.x(), m_sourceSize.width() - w);
    const int y = boundInt(0, rect.y(), m_sourceSize.height() - h);
    m_rect = snapped(QRect(x, y, w, h));
}

bool CropDraftModel::setAspect(Aspect aspect)
{
    const Aspect previous = m_aspect;
    m_aspect = aspect;
    if (!hasRect() || sourceRatio() <= 0.0) {
        return true;
    }

    const QSize size = fitSize(m_rect.width(), 0.0, m_sourceSize.width(), m_sourceSize.height());
    if (size.isEmpty()) {
        // The draft cannot take this ratio: the selected preset must not say
        // otherwise.
        m_aspect = previous;
        return false;
    }
    const qreal centerX = m_rect.x() + m_rect.width() / 2.0;
    const qreal centerY = m_rect.y() + m_rect.height() / 2.0;
    setRectClamped(QRect(qRound(centerX - size.width() / 2.0),
                         qRound(centerY - size.height() / 2.0),
                         size.width(), size.height()));
    return true;
}

void CropDraftModel::setRectFromDrag(const QPoint& anchor, const QPoint& current)
{
    if (m_sourceSize.isEmpty()) {
        m_rect = QRect();
        return;
    }
    const int maxX = m_sourceSize.width();
    const int maxY = m_sourceSize.height();
    const QPoint a(boundInt(0, anchor.x(), maxX), boundInt(0, anchor.y(), maxY));
    const QPoint c(boundInt(0, current.x(), maxX), boundInt(0, current.y(), maxY));

    const int dx = c.x() - a.x();
    const int dy = c.y() - a.y();
    // An axis the pointer has not left yet grows toward the side with room.
    const bool right = dx > 0 || (dx == 0 && a.x() < maxX);
    const bool down = dy > 0 || (dy == 0 && a.y() < maxY);
    const int availableW = right ? maxX - a.x() : a.x();
    const int availableH = down ? maxY - a.y() : a.y();

    const QSize size = fitSize(qAbs(dx), qAbs(dy), availableW, availableH);
    if (size.isEmpty() || (sourceRatio() <= 0.0 && (dx == 0 || dy == 0))) {
        m_rect = QRect();
        return;
    }
    m_rect = snapped(QRect(right ? a.x() : a.x() - size.width(),
                           down ? a.y() : a.y() - size.height(),
                           size.width(), size.height()));
}

void CropDraftModel::moveRect(const QRect& startRect, const QPoint& delta)
{
    setRectClamped(startRect.translated(delta));
}

void CropDraftModel::nudge(const QPoint& steps)
{
    if (hasRect()) {
        moveRect(m_rect, QPoint(steps.x() * m_positionStep.width(),
                                steps.y() * m_positionStep.height()));
    }
}

void CropDraftModel::resizeRect(const QRect& startRect, Handle handle, const QPoint& current)
{
    if (startRect.isEmpty() || m_sourceSize.isEmpty()) {
        return;
    }
    const int maxX = m_sourceSize.width();
    const int maxY = m_sourceSize.height();
    const int left = startRect.x();
    const int top = startRect.y();
    const int right = left + startRect.width();
    const int bottom = top + startRect.height();

    QPoint cornerAnchor;
    bool isCorner = true;
    switch (handle) {
    case Handle::TopLeft: cornerAnchor = QPoint(right, bottom); break;
    case Handle::TopRight: cornerAnchor = QPoint(left, bottom); break;
    case Handle::BottomLeft: cornerAnchor = QPoint(right, top); break;
    case Handle::BottomRight: cornerAnchor = QPoint(left, top); break;
    default: isCorner = false; break;
    }
    if (isCorner) {
        setRectFromDrag(cornerAnchor, current);
        if (!hasRect()) {
            m_rect = startRect;   // dragged onto its anchor: nothing to select
        }
        return;
    }

    const bool horizontal = handle == Handle::Left || handle == Handle::Right;
    const bool vertical = handle == Handle::Top || handle == Handle::Bottom;
    if (!horizontal && !vertical) {
        return;
    }

    // Along the dragged axis: the fixed edge and the room beyond it.
    const bool towardEnd = handle == Handle::Right || handle == Handle::Bottom;
    const int fixed = horizontal ? (towardEnd ? left : right) : (towardEnd ? top : bottom);
    const int axisMax = horizontal ? maxX : maxY;
    const int crossMax = horizontal ? maxY : maxX;
    const int available = towardEnd ? axisMax - fixed : fixed;
    const int pointer = horizontal ? current.x() : current.y();
    const int wanted = boundInt(1, towardEnd ? pointer - fixed : fixed - pointer, available);

    const qreal ratio = sourceRatio();
    int axisExtent = 0;
    int crossExtent = horizontal ? startRect.height() : startRect.width();
    int crossStart = horizontal ? top : left;
    if (ratio <= 0.0) {
        axisExtent = legalExtent(wanted, available, horizontal ? Qt::Horizontal : Qt::Vertical);
    } else {
        const QSize size = horizontal
            ? fitSize(wanted, 0.0, available, crossMax)
            : fitSize(0.0, wanted, crossMax, available);
        if (size.isEmpty()) {
            return;
        }
        axisExtent = horizontal ? size.width() : size.height();
        const int newCross = horizontal ? size.height() : size.width();
        // The other axis follows the ratio around its old center.
        const qreal center = crossStart + crossExtent / 2.0;
        crossExtent = newCross;
        crossStart = boundInt(0, qRound(center - newCross / 2.0), crossMax - newCross);
    }
    if (axisExtent <= 0) {
        return;
    }

    const int axisStart = towardEnd ? fixed : fixed - axisExtent;
    m_rect = snapped(horizontal
        ? QRect(axisStart, crossStart, axisExtent, crossExtent)
        : QRect(crossStart, axisStart, crossExtent, axisExtent));
}
