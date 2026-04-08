#include "region/SelectionDirtyRegionPlanner.h"
#include "region/SelectionDimensionLabel.h"

#include <QtGlobal>
#include <QFont>

namespace {

QRegion selectionDeltaRegion(const QRect& currentSelectionRect, const QRect& lastSelectionRect)
{
    QRegion region;
    const QRect current = currentSelectionRect.normalized();
    const QRect last = lastSelectionRect.normalized();

    if (current.isValid() && !current.isEmpty()) {
        region += QRegion(current).subtracted(QRegion(last));
    }
    if (last.isValid() && !last.isEmpty()) {
        region += QRegion(last).subtracted(QRegion(current));
    }

    return region;
}

QRegion selectionChromeRegion(const QRect& selectionRect, int margin)
{
    const QRect normalized = selectionRect.normalized();
    if (!normalized.isValid() || normalized.isEmpty()) {
        return {};
    }

    const QRect expanded = normalized.adjusted(-margin, -margin, margin, margin);
    const QRect stableInterior = normalized.adjusted(margin, margin, -margin, -margin);
    if (!stableInterior.isValid() || stableInterior.isEmpty()) {
        return QRegion(expanded);
    }

    return QRegion(expanded).subtracted(QRegion(stableInterior));
}

} // namespace

QRect SelectionDirtyRegionPlanner::magnifierRectForCursor(
    const QPoint& cursorPos, const QSize& viewportSize) const
{
    int panelX = cursorPos.x() + kMagnifierOffset;
    int panelY = cursorPos.y() + kMagnifierOffset;
    const int totalHeight = kMagnifierHeight + kMagnifierInfoHeight;

    if (panelX + kMagnifierWidth + kViewportMargin > viewportSize.width()) {
        panelX = cursorPos.x() - kMagnifierWidth - kMagnifierOffset;
    }
    if (panelY + totalHeight + kViewportMargin > viewportSize.height()) {
        panelY = cursorPos.y() - totalHeight - kMagnifierOffset;
    }

    panelX = qMax(kViewportMargin, panelX);
    panelY = qMax(kViewportMargin, panelY);

    return QRect(
        panelX - kMagnifierOuterPadding,
        panelY - kMagnifierOuterPadding,
        kMagnifierWidth + (kMagnifierOuterPadding * 2),
        totalHeight + (kMagnifierOuterPadding * 2));
}

QRect SelectionDirtyRegionPlanner::beaverRectForCursor(
    const QPoint& cursorPos, const QSize& viewportSize) const
{
    int iconX = cursorPos.x() + kBeaverOffset;
    int iconY = cursorPos.y() + kBeaverOffset;

    if (iconX + kBeaverSize + kViewportMargin > viewportSize.width()) {
        iconX = cursorPos.x() - kBeaverSize - kBeaverOffset;
    }
    if (iconY + kBeaverSize + kViewportMargin > viewportSize.height()) {
        iconY = cursorPos.y() - kBeaverSize - kBeaverOffset;
    }

    iconX = qMax(kViewportMargin, iconX);
    iconY = qMax(kViewportMargin, iconY);

    return QRect(
        iconX - kBeaverOuterPadding,
        iconY - kBeaverOuterPadding,
        kBeaverSize + (kBeaverOuterPadding * 2),
        kBeaverSize + (kBeaverOuterPadding * 2));
}

QRect SelectionDirtyRegionPlanner::cursorCompanionRectForCursor(
    RegionCaptureSettingsManager::CursorCompanionStyle style,
    const QPoint& cursorPos,
    const QSize& viewportSize) const
{
    switch (style) {
    case RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier:
        return magnifierRectForCursor(cursorPos, viewportSize);
    case RegionCaptureSettingsManager::CursorCompanionStyle::Beaver:
        return beaverRectForCursor(cursorPos, viewportSize);
    case RegionCaptureSettingsManager::CursorCompanionStyle::None:
        return QRect();
    }

    return QRect();
}

QRect SelectionDirtyRegionPlanner::dimensionInfoRectForSelection(const QRect& selectionRect,
                                                                 const QSize& viewportSize,
                                                                 qreal devicePixelRatio,
                                                                 bool ratioLocked) const
{
    if (!selectionRect.isValid()) {
        return QRect();
    }

    const QRect normalized = selectionRect.normalized();
    QFont font;
    font.setPointSize(12);
    return SelectionDimensionLabel::selectionPanelLayout(
        normalized,
        SelectionDimensionLabel::widgetLabel(normalized, devicePixelRatio),
        font,
        viewportSize,
        SelectionDimensionLabel::controlAnchorSize(ratioLocked)).panelRect;
}

QRegion SelectionDirtyRegionPlanner::planSelectionDragRegion(const SelectionDragParams& params) const
{
    QRegion dirtyRegion;

    auto addRect = [&dirtyRegion](const QRect& rect) {
        if (!rect.isEmpty()) {
            dirtyRegion += rect;
        }
    };

    dirtyRegion += selectionDeltaRegion(params.currentSelectionRect, params.lastSelectionRect);
    dirtyRegion += selectionChromeRegion(params.currentSelectionRect, kSelectionHandleMargin);
    dirtyRegion += selectionChromeRegion(params.lastSelectionRect, kSelectionHandleMargin);

    auto addPaddedDimensionInfoRect = [this, &addRect, &params](const QRect& selectionRect) {
        const QRect infoRect =
            dimensionInfoRectForSelection(
                selectionRect,
                params.viewportSize,
                params.devicePixelRatio,
                params.ratioLocked);
        if (!infoRect.isEmpty()) {
            addRect(infoRect.adjusted(
                -kDimensionInfoPadding, -kDimensionInfoPadding,
                kDimensionInfoPadding, kDimensionInfoPadding));
        }
    };
    addPaddedDimensionInfoRect(params.currentSelectionRect);
    addPaddedDimensionInfoRect(params.lastSelectionRect);

    auto addPaddedRect = [&addRect](const QRect& rect) {
        if (!rect.isEmpty()) {
            addRect(rect.adjusted(
                -kWidgetPadding, -kWidgetPadding, kWidgetPadding, kWidgetPadding));
        }
    };
    if (!params.suppressFloatingUi) {
        addPaddedRect(params.currentToolbarRect);
        addPaddedRect(params.lastToolbarRect);
        addPaddedRect(params.currentRegionControlRect);
        addPaddedRect(params.lastRegionControlRect);
    }

    return clippedToViewport(dirtyRegion, params.viewportSize);
}

QRegion SelectionDirtyRegionPlanner::planHoverRegion(const HoverParams& params) const
{
    QRegion dirtyRegion;
    return clippedToViewport(dirtyRegion, params.viewportSize);
}

QRegion SelectionDirtyRegionPlanner::clippedToViewport(
    const QRegion& region, const QSize& viewportSize) const
{
    if (region.isEmpty() || !viewportSize.isValid()) {
        return region;
    }
    return region.intersected(QRegion(QRect(QPoint(0, 0), viewportSize)));
}

QRegion SelectionDirtyRegionPlanner::crosshairStripRegion(
    const QPoint& cursorPos, const QSize& viewportSize) const
{
    if (!viewportSize.isValid()) {
        return QRegion();
    }

    const QRect viewportRect(QPoint(0, 0), viewportSize);
    if (!viewportRect.contains(cursorPos)) {
        return QRegion();
    }

    QRegion region;
    region += QRect(
        0,
        cursorPos.y() - kCrosshairMargin,
        viewportSize.width(),
        (kCrosshairMargin * 2) + 1);
    region += QRect(
        cursorPos.x() - kCrosshairMargin,
        0,
        (kCrosshairMargin * 2) + 1,
        viewportSize.height());

    return clippedToViewport(region, viewportSize);
}
