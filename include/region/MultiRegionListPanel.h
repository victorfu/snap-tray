#ifndef MULTIREGIONLISTPANEL_H
#define MULTIREGIONLISTPANEL_H

#include <QPixmap>
#include <QVector>
#include <QWidget>
#include <QHash>
#include <QString>

#include "region/MultiRegionManager.h"

class QListWidget;
class QListWidgetItem;

/**
 * @brief Left-docked list panel for multi-region capture editing.
 *
 * Provides region thumbnails, size info, drag reorder, and quick actions.
 */
class MultiRegionListPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MultiRegionListPanel(QWidget* parent = nullptr);

    void setCaptureContext(const QPixmap& background, qreal dpr);
    void setRegions(const QVector<MultiRegionManager::Region>& regions);
    void setActiveIndex(int index);
    void updatePanelGeometry(const QSize& parentSize);

signals:
    void regionActivated(int index);
    void regionDeleteRequested(int index);
    void regionReplaceRequested(int index);
    void regionMoveRequested(int fromIndex, int toIndex);

private:
    void rebuildList();
    QWidget* createItemWidget(const MultiRegionManager::Region& region);
    QPixmap thumbnailForRegion(const MultiRegionManager::Region& region) const;
    QString thumbnailCacheKey(const MultiRegionManager::Region& region) const;

    QListWidget* m_listWidget = nullptr;
    QVector<MultiRegionManager::Region> m_regions;
    QPixmap m_backgroundPixmap;
    qint64 m_backgroundCacheKey = 0;
    qreal m_devicePixelRatio = 1.0;
    bool m_updatingList = false;
    mutable QHash<QString, QPixmap> m_thumbnailCache;

    static constexpr int kPanelWidth = 280;
    static constexpr int kPanelMargin = 12;
};

#endif // MULTIREGIONLISTPANEL_H
