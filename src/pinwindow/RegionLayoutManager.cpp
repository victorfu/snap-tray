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

RegionLayoutManager::RegionLayoutManager(QObject* parent)
    : QObject(parent)
    , m_active(false)
{
}

// ============================================================================
// Mode Control
// ============================================================================

void RegionLayoutManager::enterLayoutMode(const QVector<LayoutRegion>& regions, const QSize& canvasSize)
{
    if (m_active) {
        return;
    }

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

    // Clamp to maximum canvas size
    if (newRect.right() > LayoutModeConstants::kMaxCanvasSize) {
        newRect.moveRight(LayoutModeConstants::kMaxCanvasSize);
        if (newRect.left() < 0) {
            newRect.moveLeft(0);
        }
    }
    if (newRect.bottom() > LayoutModeConstants::kMaxCanvasSize) {
        newRect.moveBottom(LayoutModeConstants::kMaxCanvasSize);
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

    QPoint delta = pos - m_state.resizeStartPos;
    QRect newRect = m_state.resizeStartRect;
    const int minSize = LayoutModeConstants::kMinRegionSize;

    // Apply resize based on edge
    switch (m_state.resizeEdge) {
        case ResizeHandler::Edge::Left:
            newRect.setLeft(newRect.left() + delta.x());
            break;
        case ResizeHandler::Edge::Right:
            newRect.setRight(newRect.right() + delta.x());
            break;
        case ResizeHandler::Edge::Top:
            newRect.setTop(newRect.top() + delta.y());
            break;
        case ResizeHandler::Edge::Bottom:
            newRect.setBottom(newRect.bottom() + delta.y());
            break;
        case ResizeHandler::Edge::TopLeft:
            newRect.setTopLeft(newRect.topLeft() + delta);
            break;
        case ResizeHandler::Edge::TopRight:
            newRect.setTop(newRect.top() + delta.y());
            newRect.setRight(newRect.right() + delta.x());
            break;
        case ResizeHandler::Edge::BottomLeft:
            newRect.setBottom(newRect.bottom() + delta.y());
            newRect.setLeft(newRect.left() + delta.x());
            break;
        case ResizeHandler::Edge::BottomRight:
            newRect.setBottomRight(newRect.bottomRight() + delta);
            break;
        default:
            return;
    }

    // Enforce minimum size
    if (newRect.width() < minSize) {
        if (m_state.resizeEdge == ResizeHandler::Edge::Left ||
            m_state.resizeEdge == ResizeHandler::Edge::TopLeft ||
            m_state.resizeEdge == ResizeHandler::Edge::BottomLeft) {
            newRect.setLeft(newRect.right() - minSize);
        } else {
            newRect.setRight(newRect.left() + minSize);
        }
    }

    if (newRect.height() < minSize) {
        if (m_state.resizeEdge == ResizeHandler::Edge::Top ||
            m_state.resizeEdge == ResizeHandler::Edge::TopLeft ||
            m_state.resizeEdge == ResizeHandler::Edge::TopRight) {
            newRect.setTop(newRect.bottom() - minSize);
        } else {
            newRect.setBottom(newRect.top() + minSize);
        }
    }

    // Maintain aspect ratio if requested (Shift key held)
    if (maintainAspectRatio && m_state.resizeStartRect.width() > 0 && m_state.resizeStartRect.height() > 0) {
        qreal aspectRatio = static_cast<qreal>(m_state.resizeStartRect.width()) /
                           static_cast<qreal>(m_state.resizeStartRect.height());

        // Determine which dimension to adjust based on edge
        bool adjustWidth = (m_state.resizeEdge == ResizeHandler::Edge::Top ||
                           m_state.resizeEdge == ResizeHandler::Edge::Bottom);
        bool adjustHeight = (m_state.resizeEdge == ResizeHandler::Edge::Left ||
                            m_state.resizeEdge == ResizeHandler::Edge::Right);

        if (adjustWidth) {
            int newWidth = static_cast<int>(newRect.height() * aspectRatio);
            newRect.setWidth(newWidth);
        } else if (adjustHeight) {
            int newHeight = static_cast<int>(newRect.width() / aspectRatio);
            newRect.setHeight(newHeight);
        } else {
            // Corner resize: use the larger delta direction
            int deltaW = std::abs(newRect.width() - m_state.resizeStartRect.width());
            int deltaH = std::abs(newRect.height() - m_state.resizeStartRect.height());

            if (deltaW > deltaH) {
                int newHeight = static_cast<int>(newRect.width() / aspectRatio);
                if (m_state.resizeEdge == ResizeHandler::Edge::TopLeft ||
                    m_state.resizeEdge == ResizeHandler::Edge::TopRight) {
                    newRect.setTop(newRect.bottom() - newHeight);
                } else {
                    newRect.setHeight(newHeight);
                }
            } else {
                int newWidth = static_cast<int>(newRect.height() * aspectRatio);
                if (m_state.resizeEdge == ResizeHandler::Edge::TopLeft ||
                    m_state.resizeEdge == ResizeHandler::Edge::BottomLeft) {
                    newRect.setLeft(newRect.right() - newWidth);
                } else {
                    newRect.setWidth(newWidth);
                }
            }
        }
    }

    // Clamp to maximum canvas size
    const bool resizeFromLeft = (m_state.resizeEdge == ResizeHandler::Edge::Left
        || m_state.resizeEdge == ResizeHandler::Edge::TopLeft
        || m_state.resizeEdge == ResizeHandler::Edge::BottomLeft);
    const bool resizeFromTop = (m_state.resizeEdge == ResizeHandler::Edge::Top
        || m_state.resizeEdge == ResizeHandler::Edge::TopLeft
        || m_state.resizeEdge == ResizeHandler::Edge::TopRight);
    const bool resizeFromRight = (m_state.resizeEdge == ResizeHandler::Edge::Right
        || m_state.resizeEdge == ResizeHandler::Edge::TopRight
        || m_state.resizeEdge == ResizeHandler::Edge::BottomRight);
    const bool resizeFromBottom = (m_state.resizeEdge == ResizeHandler::Edge::Bottom
        || m_state.resizeEdge == ResizeHandler::Edge::BottomLeft
        || m_state.resizeEdge == ResizeHandler::Edge::BottomRight);

    if (newRect.left() < 0) {
        if (resizeFromLeft) {
            newRect.setLeft(0);
        } else {
            newRect.moveLeft(0);
        }
    }
    if (newRect.top() < 0) {
        if (resizeFromTop) {
            newRect.setTop(0);
        } else {
            newRect.moveTop(0);
        }
    }

    if (newRect.right() > LayoutModeConstants::kMaxCanvasSize) {
        if (resizeFromRight) {
            newRect.setRight(LayoutModeConstants::kMaxCanvasSize);
        } else {
            newRect.moveRight(LayoutModeConstants::kMaxCanvasSize);
        }
    }
    if (newRect.bottom() > LayoutModeConstants::kMaxCanvasSize) {
        if (resizeFromBottom) {
            newRect.setBottom(LayoutModeConstants::kMaxCanvasSize);
        } else {
            newRect.moveBottom(LayoutModeConstants::kMaxCanvasSize);
        }
    }

    // Final safety clamp: moveRight/moveBottom can shift left/top negative when the
    // rect is larger than the canvas. Keep the final rect fully within bounds.
    if (newRect.left() < 0) {
        newRect.setLeft(0);
    }
    if (newRect.top() < 0) {
        newRect.setTop(0);
    }
    if (newRect.right() > LayoutModeConstants::kMaxCanvasSize) {
        newRect.setRight(LayoutModeConstants::kMaxCanvasSize);
    }
    if (newRect.bottom() > LayoutModeConstants::kMaxCanvasSize) {
        newRect.setBottom(LayoutModeConstants::kMaxCanvasSize);
    }

    m_state.regions[m_state.selectedIndex].rect = newRect;
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
    if (dpr <= 0.0 || m_state.regions.isEmpty()) {
        return QPixmap();
    }

    // Calculate bounding box of all regions
    QRect bounds;
    for (const auto& region : m_state.regions) {
        if (bounds.isNull()) {
            bounds = region.rect;
        } else {
            bounds = bounds.united(region.rect);
        }
    }

    if (bounds.isEmpty()) {
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
            finalRegion.image = std::move(materializedImage);
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
    if (m_state.regions.isEmpty() || m_state.originalSnapshot.isEmpty()) {
        return {};
    }

    QRect finalBounds;
    for (const auto& region : m_state.regions) {
        finalBounds = finalBounds.isNull()
            ? region.rect
            : finalBounds.united(region.rect);
    }
    if (finalBounds.isEmpty()) {
        return {};
    }

    const int regionCount = (std::min)(m_state.regions.size(),
                                       m_state.originalSnapshot.size());
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
        if (QRectF(m_state.originalSnapshot[i].rect).contains(originalCenterModel)) {
            regionIndex = i;
            break;
        }
    }

    QPointF targetCenterModel = originalCenterModel - outputOrigin;
    if (regionIndex >= 0) {
        const QRectF originalRegion(m_state.originalSnapshot[regionIndex].rect);
        const QRectF currentRegion(m_state.regions[regionIndex].rect);
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
        region.image.save(&buffer, "PNG");
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
