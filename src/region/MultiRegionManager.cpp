#include "region/MultiRegionManager.h"

#include <QDebug>

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

void MultiRegionManager::removeRegion(int index)
{
    if (index < 0 || index >= m_regions.size()) {
        return;
    }

    m_regions.removeAt(index);
    refreshIndices();

    if (m_regions.isEmpty()) {
        m_activeIndex = -1;
    } else if (m_activeIndex == index) {
        m_activeIndex = qMin(index, m_regions.size() - 1);
    } else if (m_activeIndex > index) {
        m_activeIndex -= 1;
    }

    setActiveIndex(m_activeIndex);
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
    if (index < -1 || index >= m_regions.size()) {
        index = -1;
    }

    if (m_activeIndex == index) {
        return;
    }

    m_activeIndex = index;
    for (int i = 0; i < m_regions.size(); ++i) {
        m_regions[i].isActive = (i == m_activeIndex);
    }
    emit activeIndexChanged(m_activeIndex);
}

QColor MultiRegionManager::nextColor() const
{
    return colorForIndex(m_regions.size());
}

void MultiRegionManager::clear()
{
    if (m_regions.isEmpty()) {
        m_activeIndex = -1;
        return;
    }
    m_regions.clear();
    m_activeIndex = -1;
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
    QPixmap merged = background.copy(
        static_cast<int>(bounds.x() * dpr),
        static_cast<int>(bounds.y() * dpr),
        static_cast<int>(bounds.width() * dpr),
        static_cast<int>(bounds.height() * dpr)
    );
    merged.setDevicePixelRatio(dpr);

    QImage image = merged.toImage();
    image.setDevicePixelRatio(dpr);
    return image;
}

QVector<QImage> MultiRegionManager::separateImages(const QPixmap& background, qreal dpr) const
{
    QVector<QImage> images;
    images.reserve(m_regions.size());

    for (const auto& region : m_regions) {
        QPixmap pixmap = background.copy(
            static_cast<int>(region.rect.x() * dpr),
            static_cast<int>(region.rect.y() * dpr),
            static_cast<int>(region.rect.width() * dpr),
            static_cast<int>(region.rect.height() * dpr)
        );
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
