#include "pinwindow/PinMergeHelper.h"

#include "PinWindow.h"
#include "pinwindow/RegionLayoutManager.h"
#include "utils/CoordinateHelper.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QSize>

namespace {
constexpr int kMaxMergeCount = LayoutModeConstants::kMaxRegionCount;

const QColor kRegionColors[] = {
    QColor(0, 174, 255),   // Blue
    QColor(52, 199, 89),   // Green
    QColor(255, 149, 0),   // Orange
    QColor(255, 59, 48),   // Red
    QColor(175, 82, 222),  // Purple
    QColor(90, 200, 250),  // Light blue
};

qreal normalizedDpr(const QPixmap& pixmap)
{
    const qreal dpr = pixmap.devicePixelRatio();
    return dpr > 0.0 ? dpr : 1.0;
}

QSize logicalSizeFromPixmap(const QPixmap& pixmap)
{
    const qreal dpr = normalizedDpr(pixmap);
    return CoordinateHelper::toLogical(pixmap.size(), dpr);
}

QPixmap normalizeToTargetDpr(const QPixmap& source, qreal targetDpr)
{
    if (source.isNull()) {
        return source;
    }

    const qreal sourceDpr = normalizedDpr(source);
    if (qFuzzyCompare(sourceDpr, targetDpr)) {
        return source;
    }

    const QSize logicalSize = logicalSizeFromPixmap(source);
    const QSize physicalSize = CoordinateHelper::toPhysical(logicalSize, targetDpr);
    const QSize physicalTargetSize(qMax(1, physicalSize.width()),
                                   qMax(1, physicalSize.height()));

    QPixmap normalized = source.scaled(physicalTargetSize,
                                       Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
    normalized.setDevicePixelRatio(targetDpr);
    return normalized;
}

struct PositionedLayoutItem {
    QPoint topLeft;
};

struct MergeLayoutGeometry {
    QVector<PositionedLayoutItem> placements;
    qint64 composedWidth = 0;
    qint64 composedHeight = 0;
};

MergeLayoutGeometry calculateLayoutGeometry(const QVector<QSize>& logicalSizes,
                                            int gap,
                                            int maxRowWidth,
                                            int rowGap)
{
    MergeLayoutGeometry geometry;
    geometry.placements.reserve(logicalSizes.size());

    const int effectiveGap = qMax(0, gap);
    const int effectiveRowGap = rowGap < 0 ? effectiveGap : qMax(0, rowGap);

    if (maxRowWidth <= 0) {
        qint64 currentX = 0;
        int maxHeight = 0;
        for (int i = 0; i < logicalSizes.size(); ++i) {
            const QSize& logicalSize = logicalSizes[i];
            geometry.placements.append({QPoint(static_cast<int>(currentX), 0)});
            currentX += logicalSize.width();
            maxHeight = qMax(maxHeight, logicalSize.height());
            if (i + 1 < logicalSizes.size()) {
                currentX += effectiveGap;
            }
        }
        geometry.composedWidth = currentX;
        geometry.composedHeight = maxHeight;
        return geometry;
    }

    qint64 maxComposedWidth = 0;
    qint64 currentX = 0;
    qint64 currentY = 0;
    qint64 currentRowWidth = 0;
    int currentRowHeight = 0;
    bool rowHasItem = false;

    for (const QSize& logicalSize : logicalSizes) {
        const qint64 itemWidth = logicalSize.width();
        const int itemHeight = logicalSize.height();

        const bool shouldWrap = rowHasItem && (currentX + itemWidth > maxRowWidth);
        if (shouldWrap) {
            maxComposedWidth = qMax(maxComposedWidth, currentRowWidth);
            currentY += currentRowHeight + effectiveRowGap;
            currentX = 0;
            currentRowWidth = 0;
            currentRowHeight = 0;
            rowHasItem = false;
        }

        geometry.placements.append({QPoint(static_cast<int>(currentX), static_cast<int>(currentY))});
        currentRowWidth = currentX + itemWidth;
        currentRowHeight = qMax(currentRowHeight, itemHeight);
        currentX = currentRowWidth + effectiveGap;
        rowHasItem = true;
    }

    if (rowHasItem) {
        maxComposedWidth = qMax(maxComposedWidth, currentRowWidth);
    }

    geometry.composedWidth = maxComposedWidth;
    geometry.composedHeight = currentY + currentRowHeight;
    return geometry;
}
}

PinMergeResult PinMergeHelper::merge(const QList<PinWindow*>& windows, int gap)
{
    PinMergeLayoutOptions options;
    options.gap = gap;
    return merge(windows, options);
}

PinMergeResult PinMergeHelper::merge(const QList<PinWindow*>& windows,
                                     const PinMergeLayoutOptions& options)
{
    PinMergeResult result;

    QList<PinWindow*> eligible;
    eligible.reserve(windows.size());
    for (PinWindow* window : sortByCreationOrder(windows)) {
        if (isEligible(window)) {
            eligible.append(window);
        }
    }

    if (eligible.size() > kMaxMergeCount) {
        result.errorMessage = QObject::tr("Cannot merge more than %1 pins at once").arg(kMaxMergeCount);
        return result;
    }

    if (eligible.size() < 2) {
        result.errorMessage = QObject::tr("Need at least 2 pins to merge");
        return result;
    }

    QList<QPixmap> sourcePixmaps;
    sourcePixmaps.reserve(eligible.size());

    QList<PinWindow*> sourceWindows;
    sourceWindows.reserve(eligible.size());

    for (PinWindow* window : eligible) {
        if (!window) {
            continue;
        }

        const QPixmap exportPixmap = window->exportPixmapForMerge();
        if (exportPixmap.isNull()) {
            continue;
        }

        sourcePixmaps.append(exportPixmap);
        sourceWindows.append(window);
    }

    if (sourcePixmaps.size() < 2) {
        result.errorMessage = QObject::tr("Need at least 2 pins to merge");
        return result;
    }

    qreal targetDpr = 1.0;
    for (const QPixmap& pixmap : sourcePixmaps) {
        targetDpr = qMax(targetDpr, normalizedDpr(pixmap));
    }

    QList<QPixmap> normalizedPixmaps;
    normalizedPixmaps.reserve(sourcePixmaps.size());
    QList<PinWindow*> mergedWindows;
    mergedWindows.reserve(sourceWindows.size());

    QVector<QSize> logicalSizes;
    logicalSizes.reserve(sourcePixmaps.size());

    const int effectiveGap = qMax(0, options.gap);
    for (int i = 0; i < sourcePixmaps.size(); ++i) {
        const QPixmap& pixmap = sourcePixmaps[i];
        const QPixmap normalized = normalizeToTargetDpr(pixmap, targetDpr);
        const QSize logicalSize = logicalSizeFromPixmap(normalized);
        if (logicalSize.width() <= 0 || logicalSize.height() <= 0) {
            continue;
        }

        normalizedPixmaps.append(normalized);
        mergedWindows.append(sourceWindows[i]);
        logicalSizes.append(logicalSize);
    }

    if (normalizedPixmaps.size() < 2 || logicalSizes.size() < 2) {
        result.errorMessage = QObject::tr("Need at least 2 pins to merge");
        return result;
    }

    const MergeLayoutGeometry geometry = calculateLayoutGeometry(
        logicalSizes, effectiveGap, options.maxRowWidth, options.rowGap);
    const qint64 composedWidth = geometry.composedWidth;
    const qint64 composedHeight = geometry.composedHeight;
    if (geometry.placements.size() != logicalSizes.size()
        || composedWidth <= 0
        || composedHeight <= 0) {
        result.errorMessage = QObject::tr("Need at least 2 pins to merge");
        return result;
    }

    if (composedWidth > LayoutModeConstants::kMaxCanvasSize
        || composedHeight > LayoutModeConstants::kMaxCanvasSize) {
        result.errorMessage = QObject::tr("Merged layout exceeds maximum size (%1x%1)")
                                  .arg(LayoutModeConstants::kMaxCanvasSize);
        return result;
    }

    const QSize composedPhysical = CoordinateHelper::toPhysical(
        QSize(static_cast<int>(composedWidth), static_cast<int>(composedHeight)), targetDpr);
    const QSize composedPhysicalSize(qMax(1, composedPhysical.width()),
                                     qMax(1, composedPhysical.height()));
    QPixmap composed(composedPhysicalSize);
    composed.setDevicePixelRatio(targetDpr);
    composed.fill(Qt::transparent);

    QPainter painter(&composed);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QVector<LayoutRegion> regions;
    regions.reserve(normalizedPixmaps.size());

    for (int i = 0; i < normalizedPixmaps.size(); ++i) {
        const QPixmap& source = normalizedPixmaps[i];
        const QSize logicalSize = logicalSizes[i];
        const QPoint topLeft = geometry.placements[i].topLeft;

        painter.drawPixmap(topLeft, source);

        LayoutRegion region;
        region.rect = QRect(topLeft, logicalSize);
        region.originalRect = region.rect;

        QImage image = source.toImage();
        image.setDevicePixelRatio(targetDpr);
        region.image = image;

        region.color = regionColor(i);
        region.index = i + 1;
        region.isSelected = false;
        regions.append(region);
    }

    painter.end();

    if (composed.isNull() || regions.size() < 2) {
        result.errorMessage = QObject::tr("Failed to merge pins");
        return result;
    }

    result.composedPixmap = composed;
    result.regions = regions;
    result.mergedWindows = mergedWindows;
    result.success = true;
    return result;
}

bool PinMergeHelper::isEligible(const PinWindow* window)
{
    return window
        && window->isVisible()
        && !window->isLiveMode();
}

QList<PinWindow*> PinMergeHelper::sortByCreationOrder(const QList<PinWindow*>& windows)
{
    // PinWindowManager appends windows in creation order, so keep list order.
    return windows;
}

QColor PinMergeHelper::regionColor(int index)
{
    constexpr int colorCount = static_cast<int>(sizeof(kRegionColors) / sizeof(kRegionColors[0]));
    if (colorCount <= 0) {
        return QColor(0, 174, 255);
    }
    return kRegionColors[index % colorCount];
}
