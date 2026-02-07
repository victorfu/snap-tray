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

    m_annotationBindings.clear();
    m_annotationOriginalRects.clear();

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

    // Clamp to maximum canvas size
    if (newRect.right() > LayoutModeConstants::kMaxCanvasSize) {
        newRect.moveRight(LayoutModeConstants::kMaxCanvasSize);
    }
    if (newRect.bottom() > LayoutModeConstants::kMaxCanvasSize) {
        newRect.moveBottom(LayoutModeConstants::kMaxCanvasSize);
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

QPixmap RegionLayoutManager::recomposeImage(qreal dpr) const
{
    if (m_state.regions.isEmpty()) {
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
    result.fill(Qt::transparent);
    result.setDevicePixelRatio(dpr);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Paint each region at its new position (using logical coordinates)
    // Note: Since result has DPR set, QPainter expects logical coordinates
    for (const auto& region : m_state.regions) {
        QRect targetRect = region.rect.translated(-bounds.topLeft());

        // Scale region image if resized
        if (region.rect.size() != region.originalRect.size()) {
            QSize targetPhysSize = CoordinateHelper::toPhysical(region.rect.size(), dpr);
            QImage scaled = region.image.scaled(
                targetPhysSize,
                Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation
            );
            scaled.setDevicePixelRatio(dpr);
            painter.drawImage(targetRect.topLeft(), scaled);
        } else {
            painter.drawImage(targetRect.topLeft(), region.image);
        }
    }

    painter.end();
    return QPixmap::fromImage(result);
}

// ============================================================================
// Annotation Integration
// ============================================================================

void RegionLayoutManager::bindAnnotations(AnnotationLayer* layer)
{
    m_annotationBindings.clear();
    m_annotationOriginalRects.clear();

    if (!layer || layer->itemCount() == 0) {
        return;
    }

    for (size_t idx = 0; idx < layer->itemCount(); ++idx) {
        AnnotationItem* annotation = layer->itemAt(static_cast<int>(idx));
        if (!annotation) {
            continue;
        }

        AnnotationRegionBinding binding;
        binding.annotation = annotation;
        binding.regionIndex = -1;

        QRectF annotationRect = annotation->boundingRect();
        m_annotationOriginalRects.append(annotationRect);
        QPointF center = annotationRect.center();

        // Find the region that contains the annotation's center
        for (int i = 0; i < m_state.regions.size(); ++i) {
            if (m_state.regions[i].rect.contains(center.toPoint())) {
                binding.regionIndex = i;
                binding.offsetFromRegion = center - m_state.regions[i].rect.topLeft();
                break;
            }
        }

        // If not in any region, bind to closest region
        if (binding.regionIndex < 0 && !m_state.regions.isEmpty()) {
            qreal minDist = (std::numeric_limits<qreal>::max)();
            for (int i = 0; i < m_state.regions.size(); ++i) {
                QPointF regionCenter = m_state.regions[i].rect.center();
                qreal dist = QLineF(center, regionCenter).length();
                if (dist < minDist) {
                    minDist = dist;
                    binding.regionIndex = i;
                    binding.offsetFromRegion = center - m_state.regions[i].rect.topLeft();
                }
            }
        }

        m_annotationBindings.append(binding);
    }
}

void RegionLayoutManager::updateAnnotationPositions()
{
    // Note: Annotation movement is tracked but not applied during layout mode.
    // The binding data is preserved for potential future use when annotations
    // gain transformation support. For now, annotations remain at their
    // original positions and are re-rendered on top of the composed image.
    Q_UNUSED(m_annotationBindings)
}

void RegionLayoutManager::restoreAnnotations(AnnotationLayer* layer)
{
    Q_UNUSED(layer)

    // Note: Since annotations are not moved during layout mode (see updateAnnotationPositions),
    // there's nothing to restore. The binding data is cleared when exiting layout mode.
    m_annotationBindings.clear();
    m_annotationOriginalRects.clear();
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

    // Normalize to origin (0,0) if regions have moved to negative coordinates
    if (bounds.left() < 0 || bounds.top() < 0) {
        QPoint offset(-std::min(0, bounds.left()), -std::min(0, bounds.top()));
        for (auto& region : m_state.regions) {
            region.rect.translate(offset);
        }
        bounds.translate(offset);
    }

    // Canvas size must contain all regions from origin (0,0)
    // bounds.size() only gives the bounding box size, not the extent from origin
    QSize newSize(bounds.right() + 1, bounds.bottom() + 1);
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
