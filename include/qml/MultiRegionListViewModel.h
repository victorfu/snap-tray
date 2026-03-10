#pragma once

#include <QObject>
#include <QPixmap>
#include <QVariantList>
#include <QVector>

#include "region/MultiRegionManager.h"

class MultiRegionListViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList regions READ regions NOTIFY regionsChanged)
    Q_PROPERTY(int activeIndex READ activeIndex WRITE setActiveIndex NOTIFY activeIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY regionsChanged)
    Q_PROPERTY(int thumbnailCacheBuster READ thumbnailCacheBuster NOTIFY thumbnailCacheBusterChanged)

public:
    explicit MultiRegionListViewModel(QObject* parent = nullptr);

    QVariantList regions() const { return m_regionsList; }
    int activeIndex() const { return m_activeIndex; }
    void setActiveIndex(int index);
    int count() const { return m_regionsList.size(); }
    int thumbnailCacheBuster() const { return m_thumbnailCacheBuster; }

    void setCaptureContext(const QPixmap& background, qreal dpr);
    void setRegions(const QVector<MultiRegionManager::Region>& regions);

    Q_INVOKABLE void activateRegion(int index);
    Q_INVOKABLE void deleteRegion(int index);
    Q_INVOKABLE void replaceRegion(int index);
    Q_INVOKABLE void moveRegion(int fromIndex, int toIndex);

signals:
    void regionActivated(int index);
    void regionDeleteRequested(int index);
    void regionReplaceRequested(int index);
    void regionMoveRequested(int fromIndex, int toIndex);
    void regionsChanged();
    void activeIndexChanged();
    void thumbnailCacheBusterChanged();

private:
    void generateThumbnails(const QVector<MultiRegionManager::Region>& regions);

    QVariantList m_regionsList;
    int m_activeIndex = -1;
    int m_thumbnailCacheBuster = 0;

    QPixmap m_backgroundPixmap;
    qint64 m_backgroundCacheKey = 0;
    qreal m_devicePixelRatio = 1.0;
};
