#include "pinwindow/RegionLayoutManager.h"
#include "utils/CoordinateHelper.h"
#include "pinwindow/RegionLayoutRenderer.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/AnnotationItem.h"

#include <QPainter>
#include <QBuffer>
#include <QDataStream>
#include <QDebug>
#include <algorithm>
#include <utility>
#include <numeric>
#include <cmath>
#include <limits>

namespace {

const QString kSourceLogicalSizeKey = QStringLiteral("SnapTray.SourceLogicalSize");

QSize sourceLogicalSize(const LayoutRegion& region)
{
    const auto parts = region.image.text(kSourceLogicalSizeKey).split(QLatin1Char('x'));
    if (parts.size() == 2) {
        const QSize recorded(parts[0].toInt(), parts[1].toInt());
        if (!recorded.isEmpty()) return recorded;
    }
    const qreal dpr = region.image.devicePixelRatio();
    if (!std::isfinite(dpr) || dpr <= 0) return {};
    const qreal width = region.image.width() / dpr;
    const qreal height = region.image.height() / dpr;
    const qreal maxRounded = std::numeric_limits<int>::max() - 1.0;
    if (!std::isfinite(width) || !std::isfinite(height)
        || width > maxRounded || height > maxRounded) return {};
    return QSize(qRound(width), qRound(height));
}

void recordSourceLogicalSize(QImage& image, const QSize& size)
{
    image.setText(kSourceLogicalSizeKey, QStringLiteral("%1x%2").arg(size.width()).arg(size.height()));
}

struct ResizeDirections
{
    int horizontal = 0;  // -1: left, +1: right
    int vertical = 0;    // -1: top, +1: bottom
};

ResizeDirections resizeDirections(ResizeHandler::Edge edge)
{
    switch (edge) {
        case ResizeHandler::Edge::TopLeft: return {-1, -1};
        case ResizeHandler::Edge::Top: return {0, -1};
        case ResizeHandler::Edge::TopRight: return {1, -1};
        case ResizeHandler::Edge::Right: return {1, 0};
        case ResizeHandler::Edge::BottomRight: return {1, 1};
        case ResizeHandler::Edge::Bottom: return {0, 1};
        case ResizeHandler::Edge::BottomLeft: return {-1, 1};
        case ResizeHandler::Edge::Left: return {-1, 0};
        default: return {};
    }
}

QRect resizedRegionRect(const QRect& startRect,
                        ResizeHandler::Edge edge,
                        const QPoint& delta,
                        bool maintainAspectRatio)
{
    const ResizeDirections directions = resizeDirections(edge);
    if (directions.horizontal == 0 && directions.vertical == 0) {
        return startRect;
    }

    constexpr qreal kRoundingEpsilon = 1e-9;
    const int minSize = LayoutModeConstants::kMinRegionSize;
    const int canvasExtent = LayoutModeConstants::kMaxCanvasSize;
    const int startWidth = startRect.width();
    const int startHeight = startRect.height();
    if (startWidth <= 0 || startHeight <= 0) {
        return startRect;
    }

    // Work with half-open bounds. QRect::right()/bottom() are inclusive, while
    // the layout canvas is [0, canvasExtent) and therefore ends at 9999.
    const int startLeft = startRect.x();
    const int startTop = startRect.y();
    const int startRight = startLeft + startWidth;
    const int startBottom = startTop + startHeight;
    const qreal centerX = startLeft + startWidth / 2.0;
    const qreal centerY = startTop + startHeight / 2.0;

    const qreal anchoredAvailableWidth = directions.horizontal < 0
        ? startRight
        : directions.horizontal > 0
            ? canvasExtent - startLeft
            : 2.0 * qMin(centerX, canvasExtent - centerX);
    const qreal anchoredAvailableHeight = directions.vertical < 0
        ? startBottom
        : directions.vertical > 0
            ? canvasExtent - startTop
            : 2.0 * qMin(centerY, canvasExtent - centerY);
    // Imported/captured regions may begin below the layout editor's 50px
    // resize minimum. In that case, expanding to the minimum takes precedence
    // over preserving an infeasible edge/center anchor exactly.
    qreal availableWidth = qMin<qreal>(
        canvasExtent,
        anchoredAvailableWidth < minSize ? canvasExtent : anchoredAvailableWidth);
    qreal availableHeight = qMin<qreal>(
        canvasExtent,
        anchoredAvailableHeight < minSize ? canvasExtent : anchoredAvailableHeight);
    if (maintainAspectRatio) {
        const qreal minScale = qMax(
            static_cast<qreal>(minSize) / startWidth,
            static_cast<qreal>(minSize) / startHeight);
        const qreal anchoredMaxScale = qMin(
            availableWidth / startWidth,
            availableHeight / startHeight);
        const qreal fullCanvasMaxScale = qMin(
            static_cast<qreal>(canvasExtent) / startWidth,
            static_cast<qreal>(canvasExtent) / startHeight);
        if (anchoredMaxScale < minScale && fullCanvasMaxScale >= minScale) {
            availableWidth = canvasExtent;
            availableHeight = canvasExtent;
        }
    }
    const int maxWidth = qMax(1, qFloor(availableWidth + kRoundingEpsilon));
    const int maxHeight = qMax(1, qFloor(availableHeight + kRoundingEpsilon));

    const qreal requestedWidth = startWidth + directions.horizontal * delta.x();
    const qreal requestedHeight = startHeight + directions.vertical * delta.y();
    int width = qBound(minSize, startWidth, maxWidth);
    int height = qBound(minSize, startHeight, maxHeight);

    if (maintainAspectRatio) {
        const qreal minScale = qMax(
            static_cast<qreal>(minSize) / startWidth,
            static_cast<qreal>(minSize) / startHeight);
        const qreal maxScale = qMin(
            availableWidth / startWidth,
            availableHeight / startHeight);
        if (maxScale + kRoundingEpsilon < minScale) {
            // The imported aspect itself cannot satisfy both the 50px
            // minimum and the 10000px canvas. Preserve minimum/bounds instead
            // of leaving the region permanently unresizable and invalid.
            return resizedRegionRect(startRect, edge, delta, false);
        }

        qreal requestedScale = 1.0;
        if (directions.horizontal != 0 && directions.vertical == 0) {
            requestedScale = requestedWidth / startWidth;
        } else if (directions.horizontal == 0 && directions.vertical != 0) {
            requestedScale = requestedHeight / startHeight;
        } else {
            // Project the pointer-requested size onto the original aspect ray.
            const qreal denominator =
                static_cast<qreal>(startWidth) * startWidth +
                static_cast<qreal>(startHeight) * startHeight;
            requestedScale =
                (requestedWidth * startWidth + requestedHeight * startHeight) /
                denominator;
        }

        const qreal scale = qBound(minScale, requestedScale, maxScale);
        width = qBound(minSize, qRound(startWidth * scale), maxWidth);
        height = qBound(minSize, qRound(startHeight * scale), maxHeight);
    } else {
        if (directions.horizontal != 0) {
            width = qBound(minSize, qRound(requestedWidth), maxWidth);
        }
        if (directions.vertical != 0) {
            height = qBound(minSize, qRound(requestedHeight), maxHeight);
        }
    }

    int left = startLeft;
    if (directions.horizontal < 0) {
        left = startRight - width;
    } else if (directions.horizontal == 0) {
        left = qRound(centerX - width / 2.0);
    }

    int top = startTop;
    if (directions.vertical < 0) {
        top = startBottom - height;
    } else if (directions.vertical == 0) {
        top = qRound(centerY - height / 2.0);
    }

    left = qBound(0, left, canvasExtent - width);
    top = qBound(0, top, canvasExtent - height);
    return QRect(left, top, width, height);
}

}  // namespace

RegionLayoutManager::RegionLayoutManager(QObject* parent)
    : QObject(parent)
    , m_active(false)
{
}

// ============================================================================
// Mode Control
// ============================================================================

void RegionLayoutManager::enterLayoutMode(const QVector<LayoutRegion>& regions, const QSize& canvasSize,
                                         qreal outputDpr, const QTransform& outputOrientation)
{
    if (m_active) {
        return;
    }

    m_freeRegions = regions;
    m_arrangementMode = 0;
    m_outputDpr = outputDpr;
    m_outputOrientation = outputOrientation;
    m_arrangementValid = true;
    m_arrangementOptions = {};
    m_arrangementOptions.order.resize(regions.size());
    std::iota(m_arrangementOptions.order.begin(), m_arrangementOptions.order.end(), 0);
    m_state.regions = regions;
    m_state.originalSnapshot = regions;
    m_state.canvasSize = canvasSize;
    m_state.originalCanvasSize = canvasSize;
    m_state.selectedIndex = -1;
    m_state.hoveredIndex = -1;
    m_state.isDragging = false;
    m_state.isResizing = false;
    m_state.resizeEdge = ResizeHandler::Edge::None;

    m_active = true;
    emit layoutChanged();
}

void RegionLayoutManager::exitLayoutMode(bool applyChanges)
{
    if (!m_active) {
        return;
    }

    if (!applyChanges) {
        // Restore original state
        m_state.regions = m_state.originalSnapshot;
        m_state.canvasSize = m_state.originalCanvasSize;
    }

    m_state.selectedIndex = -1;
    m_state.hoveredIndex = -1;
    m_state.isDragging = false;
    m_state.isResizing = false;

    m_active = false;
    emit layoutChanged();
}

bool RegionLayoutManager::isActive() const
{
    return m_active;
}

bool RegionLayoutManager::hasPendingChanges() const
{
    return m_active && (!m_arrangementValid ||
        !std::equal(m_state.regions.cbegin(), m_state.regions.cend(),
                    m_state.originalSnapshot.cbegin(), m_state.originalSnapshot.cend(),
                    [](const LayoutRegion& a, const LayoutRegion& b) {
                        return a.index == b.index && a.rect == b.rect;
                    }));
}

QVariantList RegionLayoutManager::arrangementOrder() const
{
    QVariantList result;
    for (int index : m_arrangementOptions.order) result.append(index);
    return result;
}

bool RegionLayoutManager::arrangementValid() const
{
    QRect bounds;
    for (const auto& region : m_state.regions) {
        if (region.image.isNull() || region.rect.isEmpty()) return false;
        if (region.rect.x() < 0 || region.rect.y() < 0
            || qint64(region.rect.x()) + region.rect.width() > PinLayoutGeometry::MaxCanvasExtent
            || qint64(region.rect.y()) + region.rect.height() > PinLayoutGeometry::MaxCanvasExtent)
            return false;
        bounds = bounds.united(region.rect);
    }
    return m_arrangementValid && m_state.regions.size() <= PinLayoutGeometry::MaxSources
        && PinLayoutGeometry::safeOutputSize(bounds.size(), m_outputDpr);
}

QString RegionLayoutManager::outputDescription() const
{
    if (!arrangementValid()) return tr("Layout exceeds the output limits or has invalid images.");
    QRect bounds;
    for (const auto& region : m_state.regions) bounds = bounds.united(region.rect);
    const QSize orientedSize = m_outputOrientation.mapRect(bounds).size();
    const QSize pixels = CoordinateHelper::toPhysical(orientedSize, m_outputDpr);
    return tr("Output: %1 × %2 pixels").arg(pixels.width()).arg(pixels.height());
}

void RegionLayoutManager::setArrangementMode(int mode)
{
    if (!m_active || m_state.isDragging || m_state.isResizing || mode < 0 || mode > 3
        || mode == m_arrangementMode) return;
    if (m_arrangementMode == 0) m_freeRegions = m_state.regions;
    m_arrangementMode = mode;
    updateArrangement();
}

void RegionLayoutManager::setArrangementGap(int gap)
{
    if (!m_active || gap < 0 || gap > PinLayoutGeometry::MaxCanvasExtent) return;
    m_arrangementOptions.gap = gap;
    if (m_arrangementMode != 0) updateArrangement();
    else emit layoutChanged();
}

void RegionLayoutManager::setPreventUpscale(bool prevent)
{
    if (!m_active) return;
    m_arrangementOptions.preventUpscale = prevent;
    if (m_arrangementMode != 0) updateArrangement();
    else emit layoutChanged();
}

void RegionLayoutManager::moveArrangementItem(int position, int direction)
{
    auto& order = m_arrangementOptions.order;
    if (!m_active || m_arrangementMode == 0 || (direction != -1 && direction != 1)
        || position < 0 || position >= order.size()) return;
    const int target = position + direction;
    if (target < 0 || target >= order.size()) return;
    order.swapItemsAt(position, target);
    updateArrangement();
}

void RegionLayoutManager::updateArrangement()
{
    if (m_arrangementMode == 0) {
        m_state.regions = m_freeRegions;
        m_arrangementValid = true;
    } else {
        m_arrangementOptions.mode = static_cast<PinLayoutGeometry::Mode>(m_arrangementMode - 1);
        QVector<PinLayoutGeometry::Source> sources;
        for (const auto& region : m_freeRegions) {
            sources.append({m_outputOrientation.mapRect(region.image.rect()).size(),
                region.image.devicePixelRatio(),
                m_outputOrientation.mapRect(QRect(QPoint(), sourceLogicalSize(region))).size()});
        }
        const auto geometry = PinLayoutGeometry::calculate(sources, m_outputDpr, m_arrangementOptions);
        bool invertible = false;
        const auto inverse = m_outputOrientation.inverted(&invertible);
        m_arrangementValid = geometry.valid() && invertible;
        if (m_arrangementValid) {
            // Keep vector identity stable: annotation ownership uses the source
            // index. Calculate in output space, then undo the pin's orientation
            // so Horizontal/Vertical and order describe the final visible image.
            m_state.regions = m_freeRegions;
            QRect bounds;
            for (int i = 0; i < m_state.regions.size(); ++i) {
                const QRect rect = inverse.mapRect(QRectF(geometry.placements[i])).toAlignedRect();
                m_state.regions[i].rect = rect;
                bounds = bounds.united(rect);
            }
            for (auto& region : m_state.regions) region.rect.translate(-bounds.topLeft());
        }
    }
    recalculateBounds();
    emit layoutChanged();
}

// ============================================================================
// Region Data Access
// ============================================================================

QVector<LayoutRegion> RegionLayoutManager::regions() const
{
    return m_state.regions;
}

QVector<LayoutRegion> RegionLayoutManager::originalRegions() const
{
    return m_state.originalSnapshot;
}

QRect RegionLayoutManager::canvasBounds() const
{
    return QRect(QPoint(0, 0), m_state.canvasSize);
}

// ============================================================================
// Interaction / Hit Testing
// ============================================================================

int RegionLayoutManager::hitTestRegion(const QPoint& pos) const
{
    // Iterate in reverse order so topmost (last drawn) regions are hit first
    for (int i = m_state.regions.size() - 1; i >= 0; --i) {
        if (m_state.regions[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

ResizeHandler::Edge RegionLayoutManager::hitTestHandle(const QPoint& pos) const
{
    if (m_state.selectedIndex < 0 || m_state.selectedIndex >= m_state.regions.size()) {
        return ResizeHandler::Edge::None;
    }

    // Check each handle
    static const ResizeHandler::Edge edges[] = {
        ResizeHandler::Edge::TopLeft,
        ResizeHandler::Edge::Top,
        ResizeHandler::Edge::TopRight,
        ResizeHandler::Edge::Right,
        ResizeHandler::Edge::BottomRight,
        ResizeHandler::Edge::Bottom,
        ResizeHandler::Edge::BottomLeft,
        ResizeHandler::Edge::Left
    };

    for (auto edge : edges) {
        QRect handleRect = handleRectForEdge(edge);
        if (handleRect.adjusted(-LayoutModeConstants::kHandleHitMargin,
                                -LayoutModeConstants::kHandleHitMargin,
                                LayoutModeConstants::kHandleHitMargin,
                                LayoutModeConstants::kHandleHitMargin).contains(pos)) {
            return edge;
        }
    }

    return ResizeHandler::Edge::None;
}

void RegionLayoutManager::selectRegion(int index)
{
    if (index < -1 || index >= m_state.regions.size()) {
        index = -1;
    }

    if (m_state.selectedIndex != index) {
        // Update selection state on regions
        for (int i = 0; i < m_state.regions.size(); ++i) {
            m_state.regions[i].isSelected = (i == index);
        }
        m_state.selectedIndex = index;
        emit selectionChanged(index);
        emit layoutChanged();
    }
}

int RegionLayoutManager::selectedIndex() const
{
    return m_state.selectedIndex;
}

void RegionLayoutManager::setHoveredIndex(int index)
{
    if (index < -1 || index >= m_state.regions.size()) {
        index = -1;
    }

    if (m_state.hoveredIndex != index) {
        m_state.hoveredIndex = index;
        emit layoutChanged();
    }
}

int RegionLayoutManager::hoveredIndex() const
{
    return m_state.hoveredIndex;
}

// ============================================================================
// Drag Operations
// ============================================================================

void RegionLayoutManager::startDrag(const QPoint& pos)
{
    if (m_arrangementMode != 0) return;
    if (m_state.selectedIndex < 0 || m_state.selectedIndex >= m_state.regions.size()) {
        return;
    }

    m_state.isDragging = true;
    m_state.dragStartPos = pos;
    m_state.dragStartRect = m_state.regions[m_state.selectedIndex].rect;
}

void RegionLayoutManager::updateDrag(const QPoint& pos)
{
    if (!m_state.isDragging || m_state.selectedIndex < 0) {
        return;
    }

    QPoint delta = pos - m_state.dragStartPos;
    QRect newRect = m_state.dragStartRect.translated(delta);

    // Keep regions inside the non-negative layout canvas.
    if (newRect.left() < 0) {
        newRect.moveLeft(0);
    }
    if (newRect.top() < 0) {
        newRect.moveTop(0);
    }

    // The maximum canvas is the half-open range [0, kMaxCanvasSize).
    const int maxCoordinate = LayoutModeConstants::kMaxCanvasSize - 1;
    if (newRect.right() > maxCoordinate) {
        newRect.moveRight(maxCoordinate);
        if (newRect.left() < 0) {
            newRect.moveLeft(0);
        }
    }
    if (newRect.bottom() > maxCoordinate) {
        newRect.moveBottom(maxCoordinate);
        if (newRect.top() < 0) {
            newRect.moveTop(0);
        }
    }

    m_state.regions[m_state.selectedIndex].rect = newRect;
    recalculateBounds();
    emit layoutChanged();
}

void RegionLayoutManager::finishDrag()
{
    m_state.isDragging = false;
    emit layoutChanged();
}

bool RegionLayoutManager::isDragging() const
{
    return m_state.isDragging;
}

// ============================================================================
// Resize Operations
// ============================================================================

void RegionLayoutManager::startResize(ResizeHandler::Edge edge, const QPoint& pos)
{
    if (m_arrangementMode != 0) return;
    if (m_state.selectedIndex < 0 || m_state.selectedIndex >= m_state.regions.size()) {
        return;
    }

    m_state.isResizing = true;
    m_state.resizeEdge = edge;
    m_state.resizeStartPos = pos;
    m_state.resizeStartRect = m_state.regions[m_state.selectedIndex].rect;
}

void RegionLayoutManager::updateResize(const QPoint& pos, bool maintainAspectRatio)
{
    if (!m_state.isResizing || m_state.selectedIndex < 0) {
        return;
    }

    const QPoint delta = pos - m_state.resizeStartPos;
    m_state.regions[m_state.selectedIndex].rect = resizedRegionRect(
        m_state.resizeStartRect,
        m_state.resizeEdge,
        delta,
        maintainAspectRatio);
    recalculateBounds();
    emit layoutChanged();
}

void RegionLayoutManager::finishResize()
{
    m_state.isResizing = false;
    m_state.resizeEdge = ResizeHandler::Edge::None;
    emit layoutChanged();
}

bool RegionLayoutManager::isResizing() const
{
    return m_state.isResizing;
}

// ============================================================================
// Rendering
// ============================================================================

void RegionLayoutManager::render(QPainter& painter, qreal dpr) const
{
    Q_UNUSED(dpr)
    // Delegate to RegionLayoutRenderer
    // This is called from PinWindow::paintEvent
}

// ============================================================================
// Image Recomposition
// ============================================================================

QPixmap RegionLayoutManager::recomposeImage(
    qreal dpr,
    QVector<LayoutRegion>* committedRegions) const
{
    if (m_state.regions.isEmpty() || m_state.regions.size() > PinLayoutGeometry::MaxSources
        || !m_arrangementValid) {
        return QPixmap();
    }

    // Calculate bounding box of all regions
    QRect bounds;
    for (const auto& region : m_state.regions) {
        if (region.rect.isEmpty() || region.rect.x() < 0 || region.rect.y() < 0
            || qint64(region.rect.x()) + region.rect.width() > PinLayoutGeometry::MaxCanvasExtent
            || qint64(region.rect.y()) + region.rect.height() > PinLayoutGeometry::MaxCanvasExtent) {
            return {};
        }
        if (bounds.isNull()) {
            bounds = region.rect;
        } else {
            bounds = bounds.united(region.rect);
        }
    }

    if (!PinLayoutGeometry::safeOutputSize(bounds.size(), dpr)) {
        return QPixmap();
    }

    // Create transparent canvas at physical resolution
    QSize physSize = CoordinateHelper::toPhysical(bounds.size(), dpr);
    QImage result(physSize, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) {
        return QPixmap();
    }
    result.fill(Qt::transparent);
    result.setDevicePixelRatio(dpr);

    QPainter painter(&result);
    if (!painter.isActive()) {
        return QPixmap();
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QVector<LayoutRegion> finalRegions;
    if (committedRegions) {
        finalRegions.reserve(m_state.regions.size());
    }

    // Paint each region at its new position (using logical coordinates)
    // Note: Since result has DPR set, QPainter expects logical coordinates
    for (const auto& region : m_state.regions) {
        if (region.image.isNull()) {
            painter.end();
            return QPixmap();
        }

        const QRect targetRect = region.rect.translated(-bounds.topLeft());
        const QSize targetPhysSize = CoordinateHelper::toPhysical(region.rect.size(), dpr);
        if (!targetPhysSize.isValid() || targetPhysSize.isEmpty()) {
            painter.end();
            return QPixmap();
        }

        QImage materializedImage = region.image;
        if (materializedImage.size() != targetPhysSize) {
            materializedImage = materializedImage.scaled(
                targetPhysSize,
                Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation);
        }
        if (materializedImage.isNull()) {
            painter.end();
            return QPixmap();
        }
        materializedImage.setDevicePixelRatio(dpr);
        painter.drawImage(targetRect.topLeft(), materializedImage);

        if (committedRegions) {
            LayoutRegion finalRegion = region;
            finalRegion.rect = targetRect;
            finalRegion.originalRect = targetRect;
            // Resampling belongs to the composed output only. Retain the final
            // visible source pixels so Apply/undo/history never invent detail
            // for a later no-upscale layout or repeatedly resample a source.
            finalRegions.append(std::move(finalRegion));
        }
    }

    painter.end();
    QPixmap pixmap = QPixmap::fromImage(result);
    if (pixmap.isNull()) {
        return QPixmap();
    }
    if (committedRegions) {
        *committedRegions = std::move(finalRegions);
    }
    return pixmap;
}

// ============================================================================
// Annotation Integration
// ============================================================================

void RegionLayoutManager::updateAnnotationPositions(AnnotationLayer* layer,
                                                    qreal viewScale) const
{
    if (!layer || m_state.regions.isEmpty() || m_state.originalSnapshot.isEmpty()) {
        return;
    }

    layer->translateOwnedItems(
        [this, viewScale](const AnnotationItem& annotation) {
            return annotationTranslation(annotation, viewScale);
        });
}

QPointF RegionLayoutManager::annotationTranslation(
    const AnnotationItem& annotation,
    qreal viewScale,
    bool normalizeToRecomposedOrigin) const
{
    return annotationTranslationBetween(annotation, m_state.originalSnapshot,
                                        m_state.regions, viewScale, normalizeToRecomposedOrigin);
}

QPointF RegionLayoutManager::annotationTranslationBetween(
    const AnnotationItem& annotation, const QVector<LayoutRegion>& before,
    const QVector<LayoutRegion>& after, qreal viewScale, bool normalizeToRecomposedOrigin)
{
    if (after.isEmpty() || before.isEmpty()) {
        return {};
    }

    QRect finalBounds;
    for (const auto& region : after) {
        finalBounds = finalBounds.isNull()
            ? region.rect
            : finalBounds.united(region.rect);
    }
    if (finalBounds.isEmpty()) {
        return {};
    }

    const int regionCount = (std::min)(after.size(),
                                       before.size());
    const QPointF outputOrigin = normalizeToRecomposedOrigin
        ? QPointF(finalBounds.topLeft())
        : QPointF();
    const qreal scale = viewScale > 0.0 ? viewScale : 1.0;
    const QPointF originalCenterView = QRectF(annotation.boundingRect()).center();
    const QPointF originalCenterModel = originalCenterView / scale;
    int regionIndex = -1;

    // Regions are painted in vector order, so the last containing region is
    // the visible/topmost owner for overlapping captures.
    for (int i = regionCount - 1; i >= 0; --i) {
        if (QRectF(before[i].rect).contains(originalCenterModel)) {
            regionIndex = i;
            break;
        }
    }

    QPointF targetCenterModel = originalCenterModel - outputOrigin;
    if (regionIndex >= 0) {
        const QRectF originalRegion(before[regionIndex].rect);
        const QRectF currentRegion(after[regionIndex].rect);
        if (originalRegion.width() > 0.0 && originalRegion.height() > 0.0) {
            const qreal relativeX =
                (originalCenterModel.x() - originalRegion.left()) / originalRegion.width();
            const qreal relativeY =
                (originalCenterModel.y() - originalRegion.top()) / originalRegion.height();
            targetCenterModel = QPointF(
                currentRegion.left() + relativeX * currentRegion.width(),
                currentRegion.top() + relativeY * currentRegion.height())
                - outputOrigin;
        }
    }

    // Annotation geometry and visual weight remain unchanged. Only its center
    // follows the corresponding point in the resized region.
    return targetCenterModel * scale - originalCenterView;
}

// ============================================================================
// Serialization
// ============================================================================

QByteArray RegionLayoutManager::serializeRegions(const QVector<LayoutRegion>& regions)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);

    // Write header
    stream << kSerializationMagic;
    stream << kSerializationVersion;

    // Write region count
    stream << static_cast<quint32>(regions.size());

    // Write each region
    for (const auto& region : regions) {
        stream << region.rect;
        stream << region.originalRect;
        stream << region.color;
        stream << region.index;

        // Compress image as PNG
        QByteArray imageData;
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        QImage source = region.image;
        recordSourceLogicalSize(source, sourceLogicalSize(region));
        source.save(&buffer, "PNG");
        stream << imageData;
    }

    return data;
}

QVector<LayoutRegion> RegionLayoutManager::deserializeRegions(const QByteArray& data)
{
    QVector<LayoutRegion> regions;

    if (data.isEmpty()) {
        return regions;
    }

    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_6_0);

    // Read and verify header
    quint32 magic;
    stream >> magic;
    if (magic != kSerializationMagic) {
        qWarning() << "Invalid region metadata magic number";
        return regions;
    }

    quint16 version;
    stream >> version;
    if (version != kSerializationVersion) {
        qWarning() << "Unsupported region metadata version:" << version;
        return regions;
    }

    // Read region count
    quint32 count;
    stream >> count;

    if (count > LayoutModeConstants::kMaxRegionCount) {
        qWarning() << "Region count exceeds maximum:" << count;
        return regions;
    }

    // Read each region
    for (quint32 i = 0; i < count; ++i) {
        LayoutRegion region;
        stream >> region.rect;
        stream >> region.originalRect;
        stream >> region.color;
        stream >> region.index;

        QByteArray imageData;
        stream >> imageData;
        region.image.loadFromData(imageData, "PNG");
        if (region.image.text(kSourceLogicalSizeKey).isEmpty()) {
            // Version-1 files from older builds contain materialized images
            // without DPR. Their stored logical bounds are authoritative.
            recordSourceLogicalSize(region.image, region.originalRect.size());
        }

        if (stream.status() != QDataStream::Ok) {
            qWarning() << "Error reading region data at index" << i;
            return QVector<LayoutRegion>();
        }

        regions.append(region);
    }

    return regions;
}

// ============================================================================
// Private Helpers
// ============================================================================

void RegionLayoutManager::recalculateBounds()
{
    if (m_state.regions.isEmpty()) {
        return;
    }

    QRect bounds;
    for (const auto& region : m_state.regions) {
        if (bounds.isNull()) {
            bounds = region.rect;
        } else {
            bounds = bounds.united(region.rect);
        }
    }

    // Canvas size must contain all regions from origin (0,0)
    // bounds.size() only gives the bounding box size, not the extent from origin
    QSize newSize(qMax(1, bounds.right() + 1), qMax(1, bounds.bottom() + 1));
    if (newSize != m_state.canvasSize) {
        m_state.canvasSize = newSize;
        emit canvasSizeChanged(newSize);
    }
}

QRect RegionLayoutManager::handleRectForEdge(ResizeHandler::Edge edge) const
{
    if (m_state.selectedIndex < 0 || m_state.selectedIndex >= m_state.regions.size()) {
        return QRect();
    }

    const QRect& rect = m_state.regions[m_state.selectedIndex].rect;
    const int hs = LayoutModeConstants::kHandleSize;
    const int halfHs = hs / 2;

    switch (edge) {
        case ResizeHandler::Edge::TopLeft:
            return QRect(rect.left() - halfHs, rect.top() - halfHs, hs, hs);
        case ResizeHandler::Edge::Top:
            return QRect(rect.center().x() - halfHs, rect.top() - halfHs, hs, hs);
        case ResizeHandler::Edge::TopRight:
            return QRect(rect.right() - halfHs, rect.top() - halfHs, hs, hs);
        case ResizeHandler::Edge::Right:
            return QRect(rect.right() - halfHs, rect.center().y() - halfHs, hs, hs);
        case ResizeHandler::Edge::BottomRight:
            return QRect(rect.right() - halfHs, rect.bottom() - halfHs, hs, hs);
        case ResizeHandler::Edge::Bottom:
            return QRect(rect.center().x() - halfHs, rect.bottom() - halfHs, hs, hs);
        case ResizeHandler::Edge::BottomLeft:
            return QRect(rect.left() - halfHs, rect.bottom() - halfHs, hs, hs);
        case ResizeHandler::Edge::Left:
            return QRect(rect.left() - halfHs, rect.center().y() - halfHs, hs, hs);
        default:
            return QRect();
    }
}
