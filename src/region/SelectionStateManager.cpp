#include "region/SelectionStateManager.h"

#include <QtMath>
#include <array>

namespace {

constexpr int kMinimumResizeSize = 10;
constexpr std::array<Qt::Edges, 9> kResizeEdges{{
    {}, Qt::LeftEdge | Qt::TopEdge, Qt::TopEdge, Qt::RightEdge | Qt::TopEdge,
    Qt::LeftEdge, Qt::RightEdge, Qt::LeftEdge | Qt::BottomEdge, Qt::BottomEdge,
    Qt::RightEdge | Qt::BottomEdge
}};

int centeredOrigin(int doubledCenter, int length)
{
    return qRound((doubledCenter - (length - 1)) / 2.0);
}

int maximumCenteredLength(int doubledCenter, int boundsStart, int boundsEnd)
{
    const int distanceToStart = doubledCenter - 2 * boundsStart;
    const int distanceToEnd = 2 * boundsEnd - doubledCenter;
    return qMax(0, qMin(distanceToStart, distanceToEnd) + 1);
}

template <typename RoundedDimension>
int maximumRoundedLength(int maxLength, int maxOtherLength, RoundedDimension roundedDimension)
{
    // Use the final dimension's exact rounding operation. Inverting it with
    // floating-point multiplication/division can disagree at half-pixel ties.
    int low = 0;
    int high = maxLength;
    while (low < high) {
        const int middle = low + (high - low) / 2 + 1;
        if (roundedDimension(middle) <= maxOtherLength) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

QSize ratioSizeFromHeight(int requestedHeight, qreal ratio, int maxWidth, int maxHeight,
                          int minimumSize = kMinimumResizeSize)
{
    const auto roundedWidth = [ratio](int height) { return qRound(height * ratio); };
    const int allowedHeight = maximumRoundedLength(maxHeight, maxWidth, roundedWidth);
    if (allowedHeight < minimumSize || roundedWidth(allowedHeight) < minimumSize) {
        return {};
    }
    const int minHeight = qMax(minimumSize,
        maximumRoundedLength(allowedHeight, minimumSize - 1, roundedWidth) + 1);

    const int height = qBound(minHeight, requestedHeight, allowedHeight);
    const int width = qRound(height * ratio);
    if (width < minimumSize || width > maxWidth) {
        return {};
    }
    return QSize(width, height);
}

QSize ratioSizeFromWidth(int requestedWidth, qreal ratio, int maxWidth, int maxHeight,
                          int minimumSize = kMinimumResizeSize)
{
    const auto roundedHeight = [ratio](int width) { return qRound(width / ratio); };
    const int allowedWidth = maximumRoundedLength(maxWidth, maxHeight, roundedHeight);
    if (allowedWidth < minimumSize || roundedHeight(allowedWidth) < minimumSize) {
        return {};
    }
    const int minWidth = qMax(minimumSize,
        maximumRoundedLength(allowedWidth, minimumSize - 1, roundedHeight) + 1);

    const int width = qBound(minWidth, requestedWidth, allowedWidth);
    const int height = qRound(width / ratio);
    if (height < minimumSize || height > maxHeight) {
        return {};
    }
    return QSize(width, height);
}

} // namespace

SelectionStateManager::SelectionStateManager(QObject* parent)
    : QObject(parent)
{
}

void SelectionStateManager::setState(State state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged(m_state);
    }
}

void SelectionStateManager::setSelectionRect(const QRect& rect)
{
    if (m_selectionRect != rect) {
        m_selectionRect = rect;
        m_handleCacheValid = false;  // Invalidate handle cache when selection changes
        emit selectionChanged(m_selectionRect.normalized());
    }
    // Ensure state is Complete so hasSelection() returns true
    if (rect.isValid() && !rect.isEmpty() && m_state == State::None) {
        setState(State::Complete);
    }
}

void SelectionStateManager::setBounds(const QRect& bounds)
{
    m_bounds = bounds;
}

// ============================================================================
// Selection Operations
// ============================================================================

void SelectionStateManager::startSelection(const QPoint& pos)
{
    m_startPoint = clampPointToBounds(pos);
    m_selectionRect = QRect(m_startPoint, m_startPoint);
    setState(State::Selecting);
}

void SelectionStateManager::updateSelection(const QPoint& pos)
{
    if (m_state != State::Selecting) return;

    m_selectionRect = selectionRectForDrag(m_startPoint, pos);
    m_handleCacheValid = false;  // Invalidate handle cache
    emit selectionChanged(m_selectionRect.normalized());
}

void SelectionStateManager::finishSelection()
{
    if (m_state != State::Selecting) return;

    m_selectionRect = m_selectionRect.normalized();

    // If selection is too small, clear it
    if (m_selectionRect.width() < 5 || m_selectionRect.height() < 5) {
        clearSelection();
        return;
    }

    setState(State::Complete);
}

void SelectionStateManager::clearSelection()
{
    m_selectionRect = QRect();
    m_activeHandle = ResizeHandle::None;
    setState(State::None);
    emit selectionChanged(QRect());
}

// ============================================================================
// Resize Operations
// ============================================================================

void SelectionStateManager::updateHandleRectsCache(int handleSize) const
{
    QRect rect = m_selectionRect.normalized();

    // Check if cache is still valid
    if (m_handleCacheValid && m_cachedSelectionForHandles == rect && m_cachedHandleSize == handleSize) {
        return;
    }

    int half = handleSize / 2;

    // Define handle hit areas - order matches ResizeHandle enum (minus None)
    m_cachedHandleRects[0] = QRect(rect.left() - half, rect.top() - half, handleSize, handleSize);     // TopLeft
    m_cachedHandleRects[1] = QRect(rect.center().x() - half, rect.top() - half, handleSize, handleSize); // Top
    m_cachedHandleRects[2] = QRect(rect.right() - half, rect.top() - half, handleSize, handleSize);    // TopRight
    m_cachedHandleRects[3] = QRect(rect.left() - half, rect.center().y() - half, handleSize, handleSize); // Left
    m_cachedHandleRects[4] = QRect(rect.right() - half, rect.center().y() - half, handleSize, handleSize); // Right
    m_cachedHandleRects[5] = QRect(rect.left() - half, rect.bottom() - half, handleSize, handleSize);   // BottomLeft
    m_cachedHandleRects[6] = QRect(rect.center().x() - half, rect.bottom() - half, handleSize, handleSize); // Bottom
    m_cachedHandleRects[7] = QRect(rect.right() - half, rect.bottom() - half, handleSize, handleSize);  // BottomRight

    m_cachedSelectionForHandles = rect;
    m_cachedHandleSize = handleSize;
    m_handleCacheValid = true;
}

SelectionStateManager::ResizeHandle SelectionStateManager::hitTestHandle(
    const QPoint& pos, int handleSize) const
{
    if (m_state != State::Complete) return ResizeHandle::None;

    // Update cache if needed
    updateHandleRectsCache(handleSize);

    // Check corners first (higher priority) - indices 0, 2, 5, 7
    if (m_cachedHandleRects[0].contains(pos)) return ResizeHandle::TopLeft;
    if (m_cachedHandleRects[2].contains(pos)) return ResizeHandle::TopRight;
    if (m_cachedHandleRects[5].contains(pos)) return ResizeHandle::BottomLeft;
    if (m_cachedHandleRects[7].contains(pos)) return ResizeHandle::BottomRight;

    // Check edges - indices 1, 6, 3, 4
    if (m_cachedHandleRects[1].contains(pos)) return ResizeHandle::Top;
    if (m_cachedHandleRects[6].contains(pos)) return ResizeHandle::Bottom;
    if (m_cachedHandleRects[3].contains(pos)) return ResizeHandle::Left;
    if (m_cachedHandleRects[4].contains(pos)) return ResizeHandle::Right;

    return ResizeHandle::None;
}

void SelectionStateManager::startResize(const QPoint& pos, ResizeHandle handle)
{
    if (m_state != State::Complete) return;

    m_activeHandle = handle;
    m_startPoint = pos;
    m_originalRect = m_selectionRect;
    setState(State::ResizingHandle);
}

void SelectionStateManager::updateResize(const QPoint& pos)
{
    if (m_state != State::ResizingHandle) return;

    const QRect newRect = resizedRect(m_originalRect, m_activeHandle, pos - m_startPoint);
    const QRect normalized = newRect.normalized();
    if (normalized.width() >= kMinimumResizeSize && normalized.height() >= kMinimumResizeSize) {
        m_selectionRect = newRect;
        m_handleCacheValid = false;
        emit selectionChanged(normalized);
    }
}

QRect SelectionStateManager::resizedRect(
    const QRect& originalRect, ResizeHandle handle, const QPoint& delta) const
{
    QRect newRect = originalRect;

    if (m_aspectRatio > 0.0 && isCornerHandle(handle)) {
        const QRect rect = originalRect.normalized();
        const auto edges = kResizeEdges[static_cast<std::size_t>(handle)];
        const QPoint anchor(edges.testFlag(Qt::LeftEdge) ? rect.right() : rect.left(),
                            edges.testFlag(Qt::TopEdge) ? rect.bottom() : rect.top());
        const QPoint corner(edges.testFlag(Qt::LeftEdge) ? rect.left() : rect.right(),
                            edges.testFlag(Qt::TopEdge) ? rect.top() : rect.bottom());
        // The handle's hit area extends around the corner. Preserve that press
        // offset, including when release arrives without any mouse movement.
        newRect = delta.isNull() ? rect : selectionRectForDrag(anchor, corner + delta);
    } else if (m_aspectRatio > 0.0 && !isCornerHandle(handle)) {
        // Edge resize with aspect ratio locked. The opposite edge remains fixed,
        // while the perpendicular axis stays centered on the original selection.
        // Limit the requested size before constructing the rect so clamping cannot
        // break either the ratio or those anchor semantics.
        QRect rect = originalRect.normalized();
        const int doubledCenterX = rect.left() + rect.right();
        const int doubledCenterY = rect.top() + rect.bottom();

        switch (handle) {
        case ResizeHandle::Top:
        case ResizeHandle::Bottom: {
            const bool movingTop = handle == ResizeHandle::Top;
            const int movedEdge = (movingTop ? rect.top() : rect.bottom()) + delta.y();
            const int requestedHeight = movingTop
                ? rect.bottom() - movedEdge + 1
                : movedEdge - rect.top() + 1;

            int maxHeight;
            int maxWidth;
            if (m_bounds.isEmpty()) {
                maxHeight = qMax(requestedHeight,
                                 qMax(kMinimumResizeSize,
                                      qCeil(kMinimumResizeSize / m_aspectRatio)));
                maxWidth = qMax(kMinimumResizeSize,
                                qRound(maxHeight * m_aspectRatio));
            } else {
                maxHeight = movingTop
                    ? rect.bottom() - m_bounds.top() + 1
                    : m_bounds.bottom() - rect.top() + 1;
                maxWidth = maximumCenteredLength(
                    doubledCenterX, m_bounds.left(), m_bounds.right());
            }

            const QSize boundedSize = ratioSizeFromHeight(
                requestedHeight, m_aspectRatio, maxWidth, maxHeight);
            if (boundedSize.isEmpty()) {
                break;
            }

            const int left = centeredOrigin(doubledCenterX, boundedSize.width());
            const int top = movingTop
                ? rect.bottom() - boundedSize.height() + 1
                : rect.top();
            newRect = QRect(QPoint(left, top), boundedSize);
            break;
        }
        case ResizeHandle::Left:
        case ResizeHandle::Right: {
            const bool movingLeft = handle == ResizeHandle::Left;
            const int movedEdge = (movingLeft ? rect.left() : rect.right()) + delta.x();
            const int requestedWidth = movingLeft
                ? rect.right() - movedEdge + 1
                : movedEdge - rect.left() + 1;

            int maxWidth;
            int maxHeight;
            if (m_bounds.isEmpty()) {
                maxWidth = qMax(requestedWidth,
                                qMax(kMinimumResizeSize,
                                     qCeil(kMinimumResizeSize * m_aspectRatio)));
                maxHeight = qMax(kMinimumResizeSize,
                                 qRound(maxWidth / m_aspectRatio));
            } else {
                maxWidth = movingLeft
                    ? rect.right() - m_bounds.left() + 1
                    : m_bounds.right() - rect.left() + 1;
                maxHeight = maximumCenteredLength(
                    doubledCenterY, m_bounds.top(), m_bounds.bottom());
            }

            const QSize boundedSize = ratioSizeFromWidth(
                requestedWidth, m_aspectRatio, maxWidth, maxHeight);
            if (boundedSize.isEmpty()) {
                break;
            }

            const int left = movingLeft
                ? rect.right() - boundedSize.width() + 1
                : rect.left();
            const int top = centeredOrigin(doubledCenterY, boundedSize.height());
            newRect = QRect(QPoint(left, top), boundedSize);
            break;
        }
        default:
            break;
        }
    } else {
        switch (handle) {
        case ResizeHandle::TopLeft:
            newRect.setTopLeft(originalRect.topLeft() + delta);
            break;
        case ResizeHandle::Top:
            newRect.setTop(originalRect.top() + delta.y());
            break;
        case ResizeHandle::TopRight:
            newRect.setTopRight(originalRect.topRight() + delta);
            break;
        case ResizeHandle::Left:
            newRect.setLeft(originalRect.left() + delta.x());
            break;
        case ResizeHandle::Right:
            newRect.setRight(originalRect.right() + delta.x());
            break;
        case ResizeHandle::BottomLeft:
            newRect.setBottomLeft(originalRect.bottomLeft() + delta);
            break;
        case ResizeHandle::Bottom:
            newRect.setBottom(originalRect.bottom() + delta.y());
            break;
        case ResizeHandle::BottomRight:
            newRect.setBottomRight(originalRect.bottomRight() + delta);
            break;
        default:
            break;
        }
        if (!m_bounds.isEmpty()) {
            const auto index = static_cast<std::size_t>(handle);
            const Qt::Edges edges = index < kResizeEdges.size() ? kResizeEdges[index] : Qt::Edges{};
            const QPoint topLeft = clampPointToBounds(newRect.topLeft());
            const QPoint bottomRight = clampPointToBounds(newRect.bottomRight());
            if (edges.testFlag(Qt::LeftEdge)) newRect.setLeft(topLeft.x());
            if (edges.testFlag(Qt::TopEdge)) newRect.setTop(topLeft.y());
            if (edges.testFlag(Qt::RightEdge)) newRect.setRight(bottomRight.x());
            if (edges.testFlag(Qt::BottomEdge)) newRect.setBottom(bottomRight.y());
        }
    }

    return newRect;
}

void SelectionStateManager::resizeToPosition(const QPoint& pos, ResizeHandle handle)
{
    if (!isComplete() || m_selectionRect.isEmpty() || handle == ResizeHandle::None) {
        return;
    }
    const QRect rect = selectionRect();
    // Project the click onto the current edge/corner so a zero delta means
    // the existing geometry, just as it does at the start of a handle drag.
    const QPoint edgePoint(qBound(rect.left(), pos.x(), rect.right()),
                           qBound(rect.top(), pos.y(), rect.bottom()));
    const QRect resized = resizedRect(rect, handle, pos - edgePoint).normalized();
    // Outside clicks only expand. Switching the driving axis of a rounded
    // ratio at a perpendicular bound must not shrink either dimension.
    if (resized.width() < rect.width() || resized.height() < rect.height()) {
        return;
    }
    if (resized.width() >= kMinimumResizeSize && resized.height() >= kMinimumResizeSize) {
        setSelectionRect(resized);
    }
}

void SelectionStateManager::finishResize()
{
    if (m_state != State::ResizingHandle) return;

    m_selectionRect = m_selectionRect.normalized();
    m_activeHandle = ResizeHandle::None;
    setState(State::Complete);
}

bool SelectionStateManager::resizeFromBottomRight(
    const QPoint& edgeDelta,
    int minimumSize)
{
    if (!isComplete() || m_selectionRect.isEmpty() || edgeDelta.isNull()) {
        return false;
    }

    minimumSize = qMax(1, minimumSize);
    const QRect original = m_selectionRect.normalized();
    QRect resized = original;
    int newRight = resized.right() + edgeDelta.x();
    int newBottom = resized.bottom() + edgeDelta.y();

    if (!m_bounds.isEmpty()) {
        newRight = qMin(newRight, m_bounds.right());
        newBottom = qMin(newBottom, m_bounds.bottom());
    }

    if (newRight - resized.left() + 1 < minimumSize
        || newBottom - resized.top() + 1 < minimumSize) {
        return false;
    }

    resized.setRight(newRight);
    resized.setBottom(newBottom);
    if (m_aspectRatio > 0.0) {
        // Keyboard resize anchors the top-left, unlike centered edge dragging.
        // Derive the other dimension from the requested axis before applying
        // bounds, so growing one axis also grows the other when space permits.
        const int maxWidth = m_bounds.isEmpty()
            ? qMax(resized.width(), qRound(resized.height() * m_aspectRatio))
            : m_bounds.right() - resized.left() + 1;
        const int maxHeight = m_bounds.isEmpty()
            ? qMax(resized.height(), qRound(resized.width() / m_aspectRatio))
            : m_bounds.bottom() - resized.top() + 1;
        QSize size;
        if (edgeDelta.y() == 0) {
            size = ratioSizeFromWidth(resized.width(), m_aspectRatio, maxWidth, maxHeight, minimumSize);
        } else if (edgeDelta.x() == 0) {
            size = ratioSizeFromHeight(resized.height(), m_aspectRatio, maxWidth, maxHeight, minimumSize);
        } else {
            size = ratioSizeFromWidth(resized.width(), m_aspectRatio,
                                      qMin(maxWidth, resized.width()),
                                      qMin(maxHeight, resized.height()), minimumSize);
        }
        if (size.isEmpty()) {
            return false;
        }
        // Switching the driving axis after rounding must not turn a grow
        // request into a shrink on either axis, including at a saturated bound.
        const bool growingOnly = edgeDelta.x() >= 0 && edgeDelta.y() >= 0;
        if (growingOnly
            && (size.width() < original.width() || size.height() < original.height())) {
            return false;
        }
        resized.setSize(size);
    }
    if (resized == original) {
        return false;
    }

    setSelectionRect(resized);
    return true;
}

// ============================================================================
// Move Operations
// ============================================================================

bool SelectionStateManager::hitTestMove(const QPoint& pos) const
{
    if (m_state != State::Complete) return false;
    return m_selectionRect.normalized().contains(pos);
}

void SelectionStateManager::startMove(const QPoint& pos)
{
    if (m_state != State::Complete) return;

    m_startPoint = pos;
    m_originalRect = m_selectionRect;
    setState(State::Moving);
}

void SelectionStateManager::updateMove(const QPoint& pos)
{
    if (m_state != State::Moving) return;

    QPoint delta = pos - m_startPoint;
    m_selectionRect = m_originalRect.translated(delta);

    // Clamp to bounds
    clampToBounds();

    m_handleCacheValid = false;  // Invalidate handle cache
    emit selectionChanged(m_selectionRect.normalized());
}

void SelectionStateManager::finishMove()
{
    if (m_state != State::Moving) return;

    m_selectionRect = m_selectionRect.normalized();
    setState(State::Complete);
}

// ============================================================================
// Window Detection Support
// ============================================================================

void SelectionStateManager::setFromDetectedWindow(const QRect& windowRect)
{
    m_selectionRect = windowRect;
    clampToBounds();
    setState(State::Complete);
    emit selectionChanged(m_selectionRect.normalized());
}

// ============================================================================
// Cancel/Restore
// ============================================================================

void SelectionStateManager::cancelResizeOrMove()
{
    if (m_state == State::ResizingHandle || m_state == State::Moving) {
        m_selectionRect = m_originalRect;
        m_activeHandle = ResizeHandle::None;
        setState(State::Complete);
        emit selectionChanged(m_selectionRect.normalized());
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

void SelectionStateManager::clampToBounds()
{
    if (m_bounds.isEmpty()) return;

    if (m_selectionRect.left() < m_bounds.left())
        m_selectionRect.moveLeft(m_bounds.left());
    if (m_selectionRect.top() < m_bounds.top())
        m_selectionRect.moveTop(m_bounds.top());
    if (m_selectionRect.right() > m_bounds.right())
        m_selectionRect.moveRight(m_bounds.right());
    if (m_selectionRect.bottom() > m_bounds.bottom())
        m_selectionRect.moveBottom(m_bounds.bottom());
}

QPoint SelectionStateManager::clampPointToBounds(const QPoint& point) const
{
    if (m_bounds.isEmpty()) return point;
    return QPoint(qBound(m_bounds.left(), point.x(), m_bounds.right()),
                  qBound(m_bounds.top(), point.y(), m_bounds.bottom()));
}

QRect SelectionStateManager::selectionRectForDrag(const QPoint& anchor, const QPoint& point) const
{
    const QPoint bounded = clampPointToBounds(point);
    if (m_aspectRatio <= 0.0) return QRect(anchor, bounded);
    if (m_bounds.isEmpty())
        return QRect(anchor, anchor + adjustDeltaForAspectRatio(point - anchor));

    // Work with inclusive rectangle sizes, not signed QPoint deltas: QRect's
    // negative-size normalization otherwise shifts the fixed anchor and gives
    // different ratios in different drag quadrants.
    int width = qAbs(bounded.x() - anchor.x()) + 1;
    int height = qAbs(bounded.y() - anchor.y()) + 1;
    if (qreal(width) / height > m_aspectRatio)
        width = qMax(1, qRound(height * m_aspectRatio));
    else
        height = qMax(1, qRound(width / m_aspectRatio));
    const int left = bounded.x() < anchor.x() ? anchor.x() - width + 1 : anchor.x();
    const int top = bounded.y() < anchor.y() ? anchor.y() - height + 1 : anchor.y();
    return QRect(left, top, width, height);
}

QPoint SelectionStateManager::adjustDeltaForAspectRatio(const QPoint& delta) const
{
    if (m_aspectRatio <= 0.0) {
        return delta;
    }

    int dx = delta.x();
    int dy = delta.y();
    if (dx == 0 && dy == 0) {
        return delta;
    }

    double absDx = qAbs(dx);
    double absDy = qAbs(dy);
    if (absDx < 1.0) absDx = 1.0;
    if (absDy < 1.0) absDy = 1.0;

    if (absDx / absDy > m_aspectRatio) {
        absDx = absDy * m_aspectRatio;
    } else {
        absDy = absDx / m_aspectRatio;
    }

    int adjDx = (dx < 0 ? -1 : 1) * static_cast<int>(absDx + 0.5);
    int adjDy = (dy < 0 ? -1 : 1) * static_cast<int>(absDy + 0.5);
    return QPoint(adjDx, adjDy);
}

bool SelectionStateManager::isCornerHandle(ResizeHandle handle) const
{
    return handle == ResizeHandle::TopLeft ||
           handle == ResizeHandle::TopRight ||
           handle == ResizeHandle::BottomLeft ||
           handle == ResizeHandle::BottomRight;
}
