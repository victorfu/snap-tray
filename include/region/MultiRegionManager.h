#ifndef MULTIREGIONMANAGER_H
#define MULTIREGIONMANAGER_H

#include <QObject>
#include <QRect>
#include <QColor>
#include <QVector>
#include <QImage>
#include <QPixmap>

/**
 * @brief Manages multiple selection regions for multi-region capture.
 */
class MultiRegionManager : public QObject
{
    Q_OBJECT

public:
    struct Region {
        QRect rect;
        QColor color;
        int index = 0;      // 1-based display index
        bool isActive = false;
    };

    explicit MultiRegionManager(QObject* parent = nullptr);

    int addRegion(const QRect& rect);
    bool moveRegion(int fromIndex, int toIndex);
    void removeRegion(int index);
    void updateRegion(int index, const QRect& rect);
    int hitTest(const QPoint& pos) const;

    QVector<Region> regions() const { return m_regions; }
    QRect regionRect(int index) const;

    int activeIndex() const { return m_activeIndex; }
    void setActiveIndex(int index);

    int count() const { return m_regions.size(); }
    int nextIndex() const { return m_regions.size() + 1; }
    QColor nextColor() const;

    void clear();
    QRect boundingBox() const;

    // Output helpers
    QImage mergeToSingleImage(const QPixmap& background, qreal dpr) const;
    QVector<QImage> separateImages(const QPixmap& background, qreal dpr) const;

signals:
    void regionAdded(int index);
    void regionsReordered();
    void regionRemoved(int index);
    void regionUpdated(int index);
    void activeIndexChanged(int index);
    void regionsCleared();

private:
    QColor colorForIndex(int index) const;
    void refreshIndices();
    void applyActiveIndex(int index, bool forceSignal);

    QVector<Region> m_regions;
    int m_activeIndex = -1;
};

#endif // MULTIREGIONMANAGER_H
