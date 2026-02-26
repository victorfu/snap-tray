#ifndef PINMERGEHELPER_H
#define PINMERGEHELPER_H

#include <QList>
#include <QColor>
#include <QPixmap>
#include <QString>
#include <QVector>

#include "pinwindow/RegionLayoutManager.h"

class PinWindow;

namespace PinMergeConstants {
constexpr int kDefaultGap = 8;
}

struct PinMergeLayoutOptions {
    int gap = PinMergeConstants::kDefaultGap;
    int maxRowWidth = 0;  // <= 0 keeps legacy single-row behavior.
    int rowGap = -1;      // < 0 falls back to gap.
};

struct PinMergeResult {
    QPixmap composedPixmap;
    QVector<LayoutRegion> regions;
    QList<PinWindow*> mergedWindows;
    bool success = false;
    QString errorMessage;
};

class PinMergeHelper
{
public:
    static PinMergeResult merge(const QList<PinWindow*>& windows,
                                int gap = PinMergeConstants::kDefaultGap);
    static PinMergeResult merge(const QList<PinWindow*>& windows,
                                const PinMergeLayoutOptions& options);

private:
    static bool isEligible(const PinWindow* window);
    static QList<PinWindow*> sortByCreationOrder(const QList<PinWindow*>& windows);
    static QColor regionColor(int index);
};

#endif // PINMERGEHELPER_H
