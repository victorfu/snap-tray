#include "region/MultiRegionManager.h"
#include "utils/CoordinateHelper.h"

#include <QDebug>
#include <QPainter>

namespace {
const QVector<QColor> kRegionColors = {
    QColor(0, 174, 255),   // Blue
    QColor(52, 199, 89),   // Green
    QColor(255, 149, 0),   // Orange
    QColor(255, 59, 48),   // Red
    QColor(175, 82, 222),  // Purple
    QColor(90, 200, 250)   // Light blue
};
}

MultiRegionManager::MultiRegionManager(QObject* parent)
    : QObject(parent)
{
}

int MultiRegionManager::addRegion(const QRect& rect)
{
    if (!rect.isValid() || rect.isEmpty()) {
        return -1;
    }

    Region region;
    region.rect = rect.normalized();
    region.index = m_regions.size() + 1;
    region.color = colorForIndex(m_regions.size());
    region.isActive = false;

    m_regions.push_back(region);
    int newIndex = m_regions.size() - 1;
    setActiveIndex(newIndex);
    emit regionAdded(newIndex);
    return newIndex;
}

bool MultiRegionManager::moveRegion(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_regions.size() ||
        toIndex < 0 || toIndex >= m_regions.size() ||
        fromIndex == toIndex) {
        return false;
    }

    const Region movedRegion = m_regions[fromIndex];
    m_regions.removeAt(fromIndex);
    m_regions.insert(toIndex, movedRegion);

    const int oldActiveIndex = m_activeIndex;
    int newActiveIndex = oldActiveIndex;
    if (oldActiveIndex == fromIndex) {
        newActiveIndex = toIndex;
    } else if (fromIndex < toIndex && oldActiveIndex > fromIndex && oldActiveIndex <= toIndex) {
        newActiveIndex = oldActiveIndex - 1;
    } else if (toIndex < fromIndex && oldActiveIndex >= toIndex && oldActiveIndex < fromIndex) {
        newActiveIndex = oldActiveIndex + 1;
    }

    applyActiveState(newActiveIndex, false);
    emit regionsReordered();
    return true;
}

void MultiRegionManager::removeRegion(int index)
{
    if (index < 0 || index >= m_regions.size()) {
        return;
    }

    const int oldActiveIndex = m_activeIndex;
    const bool removedWasActive = (oldActiveIndex == index);

    m_regions.removeAt(index);
    refreshIndices();

    int newActiveIndex = oldActiveIndex;
    if (m_regions.isEmpty()) {
        newActiveIndex = -1;
    } else if (oldActiveIndex == index) {
        newActiveIndex = qMin(index, m_regions.size() - 1);
    } else if (oldActiveIndex > index) {
        newActiveIndex = oldActiveIndex - 1;
    }

    applyActiveState(newActiveIndex, removedWasActive);
    emit regionRemoved(index);
}

void MultiRegionManager::updateRegion(int index, const QRect& rect)
{
    if (index < 0 || index >= m_regions.size()) {
        return;
    }

    QRect normalized = rect.normalized();
    if (!normalized.isValid() || normalized.isEmpty()) {
        return;
    }

    m_regions[index].rect = normalized;
    emit regionUpdated(index);
}

int MultiRegionManager::hitTest(const QPoint& pos) const
{
    for (int i = m_regions.size() - 1; i >= 0; --i) {
        if (m_regions[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

QRect MultiRegionManager::regionRect(int index) const
{
    if (index < 0 || index >= m_regions.size()) {
        return QRect();
    }
    return m_regions[index].rect;
}

void MultiRegionManager::setActiveIndex(int index)
{
    applyActiveState(index, false);
}

void MultiRegionManager::applyActiveState(int index, bool emitOnUnchangedIndex)
{
    if (index < -1 || index >= m_regions.size()) {
        index = -1;
    }

    const bool shouldEmit = (m_activeIndex != index) || emitOnUnchangedIndex;
    m_activeIndex = index;
    for (int i = 0; i < m_regions.size(); ++i) {
        m_regions[i].isActive = (i == m_activeIndex);
    }
    if (shouldEmit) {
        emit activeIndexChanged(m_activeIndex);
    }
}

QColor MultiRegionManager::nextColor() const
{
    return colorForIndex(m_regions.size());
}

void MultiRegionManager::clear()
{
    if (m_regions.isEmpty()) {
        applyActiveState(-1, false);
        return;
    }
    const bool hadActive = (m_activeIndex != -1);
    m_regions.clear();
    applyActiveState(-1, hadActive);
    emit regionsCleared();
}

QRect MultiRegionManager::boundingBox() const
{
    if (m_regions.isEmpty()) {
        return QRect();
    }

    QRect bounds = m_regions.first().rect;
    for (int i = 1; i < m_regions.size(); ++i) {
        bounds = bounds.united(m_regions[i].rect);
    }
    return bounds;
}

QImage MultiRegionManager::mergeToSingleImage(const QPixmap& background, qreal dpr) const
{
    if (m_regions.isEmpty()) {
        return QImage();
    }

    QRect bounds = boundingBox();

    // Create a transparent image of the bounding box size (in physical pixels)
    QSize physSize = CoordinateHelper::toPhysical(bounds.size(), dpr);

    QImage result(physSize, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setRenderHint(QPainter::Antialiasing, false);

    for (const auto& region : m_regions) {
        // Extract raw pixels for this region from background
        // We assume background.copy() with these coordinates extracts the correct raw pixels
        // matching the region's position on screen.
        QRect physRegionRect = CoordinateHelper::toPhysical(region.rect, dpr);
        QPixmap regionPix = background.copy(physRegionRect);

        // Ensure we treat this as a raw bitmap for 1:1 copying
        regionPix.setDevicePixelRatio(1.0);

        // Calculate target position in the result image (physical coords)
        QPoint offset(region.rect.x() - bounds.x(), region.rect.y() - bounds.y());
        QPoint targetPos = CoordinateHelper::toPhysical(offset, dpr);

        painter.drawPixmap(targetPos, regionPix);
    }

    painter.end();

    // Set the correct DPR on the result so it renders correctly in logical coords later
    result.setDevicePixelRatio(dpr);
    return result;
}

QVector<QImage> MultiRegionManager::separateImages(const QPixmap& background, qreal dpr) const
{
    QVector<QImage> images;
    images.reserve(m_regions.size());

    for (const auto& region : m_regions) {
        QRect physRegionRect = CoordinateHelper::toPhysical(region.rect, dpr);
        QPixmap pixmap = background.copy(physRegionRect);
        pixmap.setDevicePixelRatio(dpr);

        QImage image = pixmap.toImage();
        image.setDevicePixelRatio(dpr);
        images.push_back(image);
    }

    return images;
}

QColor MultiRegionManager::colorForIndex(int index) const
{
    if (kRegionColors.isEmpty()) {
        return QColor(0, 174, 255);
    }
    return kRegionColors[index % kRegionColors.size()];
}

void MultiRegionManager::refreshIndices()
{
    for (int i = 0; i < m_regions.size(); ++i) {
        m_regions[i].index = i + 1;
        m_regions[i].color = colorForIndex(i);
    }
}
