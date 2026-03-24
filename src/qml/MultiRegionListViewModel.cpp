#include "qml/MultiRegionListViewModel.h"
#include "qml/DialogImageProvider.h"
#include "utils/CoordinateHelper.h"

#include <QPainter>
#include <QVariantMap>

namespace {
constexpr int kThumbnailWidth = 72;
constexpr int kThumbnailHeight = 46;
} // namespace

MultiRegionListViewModel::MultiRegionListViewModel(QObject* parent)
    : QObject(parent)
{
}

void MultiRegionListViewModel::setActiveIndex(int index)
{
    if (m_activeIndex == index)
        return;
    m_activeIndex = index;
    emit activeIndexChanged();
}

void MultiRegionListViewModel::setCaptureContext(const QPixmap& background, qreal dpr)
{
    const qreal normalizedDpr = dpr > 0.0 ? dpr : 1.0;
    m_backgroundPixmap = background;
    m_devicePixelRatio = normalizedDpr;
}

void MultiRegionListViewModel::setRegions(const QVector<MultiRegionManager::Region>& regions)
{
    generateThumbnails(regions);

    m_regionsList.clear();
    for (const auto& region : regions) {
        QVariantMap map;
        map[QStringLiteral("index")] = region.index;
        map[QStringLiteral("width")] = region.rect.width();
        map[QStringLiteral("height")] = region.rect.height();
        map[QStringLiteral("colorHex")] = region.color.name();
        map[QStringLiteral("thumbnailId")] = QStringLiteral("region_%1").arg(region.index);
        m_regionsList.append(map);
    }

    m_thumbnailCacheBuster++;
    emit thumbnailCacheBusterChanged();
    emit regionsChanged();
}

void MultiRegionListViewModel::activateRegion(int index)
{
    emit regionActivated(index);
}

void MultiRegionListViewModel::deleteRegion(int index)
{
    emit regionDeleteRequested(index);
}

void MultiRegionListViewModel::replaceRegion(int index)
{
    emit regionReplaceRequested(index);
}

void MultiRegionListViewModel::moveRegion(int fromIndex, int toIndex)
{
    emit regionMoveRequested(fromIndex, toIndex);
}

void MultiRegionListViewModel::generateThumbnails(const QVector<MultiRegionManager::Region>& regions)
{
    if (m_backgroundPixmap.isNull())
        return;

    for (const auto& region : regions) {
        if (!region.rect.isValid() || region.rect.isEmpty())
            continue;

        const qreal dpr = m_devicePixelRatio > 0.0 ? m_devicePixelRatio : 1.0;
        const QRect physicalRect = CoordinateHelper::toPhysicalCoveringRect(region.rect, dpr);
        if (!physicalRect.isValid() || physicalRect.isEmpty())
            continue;

        QImage thumbnail(kThumbnailWidth * 2, kThumbnailHeight * 2, QImage::Format_ARGB32_Premultiplied);
        thumbnail.fill(Qt::transparent);

        QPainter painter(&thumbnail);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(QRect(0, 0, thumbnail.width(), thumbnail.height()),
                           m_backgroundPixmap, physicalRect);
        painter.end();

        const QString id = QStringLiteral("region_%1").arg(region.index);
        SnapTray::DialogImageProvider::setImage(id, thumbnail);
    }
}
