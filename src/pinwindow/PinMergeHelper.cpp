#include "pinwindow/PinMergeHelper.h"
#include "pinwindow/PinLayoutGeometry.h"

#include "PinWindow.h"
#include "pinwindow/RegionLayoutManager.h"
#include "ui/DesignSystem.h"
#include "utils/CoordinateHelper.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QSize>

namespace {
constexpr int kMaxMergeCount = LayoutModeConstants::kMaxRegionCount;

const QColor kRegionColors[] = {
    QColor(52, 199, 89),   // Green
    QColor(255, 149, 0),   // Orange
    QColor(255, 59, 48),   // Red
    QColor(0, 174, 255),   // Blue
    QColor(90, 200, 250),  // Light blue
};
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

    // Validate every source before taking any snapshots. Skipping a busy pin
    // could silently produce a partial merge and discard its pending redaction.
    for (const PinWindow* window : eligible) {
        if (window->isAutoBlurInProgress()) {
            result.errorMessage = QCoreApplication::translate(
                "PinWindow", "Please wait for auto-blur to finish");
            return result;
        }
    }

    QVector<PinLayoutGeometry::Source> sources;
    qreal targetDpr = 1.0;
    for (const auto* window : eligible) {
        sources.append(window->mergeSourceGeometry());
        targetDpr = qMax(targetDpr, sources.last().dpr);
    }
    PinLayoutGeometry::Options geometryOptions;
    geometryOptions.gap = qMax(0, options.gap);
    geometryOptions.maxRowWidth = options.maxRowWidth;
    geometryOptions.rowGap = options.rowGap;
    geometryOptions.preventUpscale = false; // preserve the existing merge default
    const auto geometry = PinLayoutGeometry::calculate(sources, targetDpr, geometryOptions);
    if (!geometry.valid()) {
        result.errorMessage = QCoreApplication::translate("RegionLayoutManager",
            "Layout exceeds the output limits or has invalid images.");
        return result;
    }

    // Reject the aggregate geometry before allocating any rendered snapshot.
    QList<QPixmap> sourcePixmaps;
    sourcePixmaps.reserve(eligible.size());
    for (int i = 0; i < eligible.size(); ++i) {
        const QPixmap source = eligible[i]->exportPixmapForMerge();
        if (source.isNull() || source.size() != sources[i].pixels
            || !qFuzzyCompare(source.devicePixelRatio(), sources[i].dpr)) {
            result.errorMessage = QObject::tr("Failed to merge pins");
            return result;
        }
        sourcePixmaps.append(source);
    }

    QPixmap composed(geometry.physicalSize);
    if (composed.isNull()) {
        result.errorMessage = QObject::tr("Failed to merge pins");
        return result;
    }
    composed.setDevicePixelRatio(targetDpr);
    composed.fill(Qt::transparent);
    QPainter painter(&composed);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QVector<LayoutRegion> regions;
    for (int i = 0; i < sourcePixmaps.size(); ++i) {
        const QPixmap& source = sourcePixmaps[i];
        painter.drawPixmap(geometry.placements[i], source, source.rect());
        LayoutRegion region;
        region.rect = geometry.placements[i];
        region.originalRect = region.rect;
        // Keep final exported, redacted pixels at native resolution. Later
        // no-upscale layouts must not mistake normalized pixels for detail.
        region.image = source.toImage();
        region.color = regionColor(i);
        region.index = i + 1;
        regions.append(region);
    }

    painter.end();

    if (composed.isNull() || regions.size() < 2) {
        result.errorMessage = QObject::tr("Failed to merge pins");
        return result;
    }

    result.composedPixmap = composed;
    result.regions = regions;
    result.mergedWindows = eligible;
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
    if (index <= 0) {
        return DesignSystem::instance().captureSelectionAccent();
    }

    constexpr int colorCount = static_cast<int>(sizeof(kRegionColors) / sizeof(kRegionColors[0]));
    if (colorCount <= 0) {
        return DesignSystem::instance().captureSelectionAccent();
    }
    return kRegionColors[(index - 1) % colorCount];
}
