#include "region/SelectionDirtyRegionPlanner.h"

#include <QtGlobal>
#include <QFont>
#include <QFontMetrics>
#include <QString>

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

QRect SelectionDirtyRegionPlanner::dimensionInfoRectForSelection(const QRect& selectionRect) const
{
    if (!selectionRect.isValid()) {
        return QRect();
    }

    const QRect normalized = selectionRect.normalized();
    QFont font;
    font.setPointSize(12);
    QFontMetrics fm(font);

    const QString dimensionsLabel = QStringLiteral("%1 x %2  pt")
        .arg(normalized.width())
        .arg(normalized.height());
    const int fixedWidth = fm.horizontalAdvance(QStringLiteral("9999 x 9999  pt")) + 24;
    const int actualWidth = fm.horizontalAdvance(dimensionsLabel) + 24;
    const int panelWidth = qMax(fixedWidth, actualWidth);
    const int panelHeight = 28;

    int textX = normalized.left();
    int textY = normalized.top() - panelHeight - 8;
    if (textY < 5) {
        textY = normalized.top() + 5;
        textX = normalized.left() + 5;
    }

    return QRect(textX, textY, panelWidth, panelHeight);
}

QRegion SelectionDirtyRegionPlanner::planSelectionDragRegion(const SelectionDragParams& params) const
{
    QRegion dirtyRegion;

    auto addRect = [&dirtyRegion](const QRect& rect) {
        if (!rect.isEmpty()) {
            dirtyRegion += rect;
        }
    };

    if (params.currentSelectionRect.isValid()) {
        addRect(params.currentSelectionRect.normalized().adjusted(
            -kSelectionHandleMargin, -kSelectionHandleMargin,
            kSelectionHandleMargin, kSelectionHandleMargin));
    }
    if (params.lastSelectionRect.isValid()) {
        addRect(params.lastSelectionRect.normalized().adjusted(
            -kSelectionHandleMargin, -kSelectionHandleMargin,
            kSelectionHandleMargin, kSelectionHandleMargin));
    }

    addRect(dimensionInfoRectForSelection(params.currentSelectionRect));
    addRect(dimensionInfoRectForSelection(params.lastSelectionRect));

    if (params.includeMagnifier) {
        addRect(params.currentMagnifierRect);
        addRect(params.lastMagnifierRect);
    }

    auto addPaddedRect = [&addRect](const QRect& rect) {
        if (!rect.isEmpty()) {
            addRect(rect.adjusted(
                -kWidgetPadding, -kWidgetPadding, kWidgetPadding, kWidgetPadding));
        }
    };
    addPaddedRect(params.currentToolbarRect);
    addPaddedRect(params.lastToolbarRect);
    addPaddedRect(params.currentRegionControlRect);
    addPaddedRect(params.lastRegionControlRect);

    return clippedToViewport(dirtyRegion, params.viewportSize);
}

QRegion SelectionDirtyRegionPlanner::planHoverRegion(const HoverParams& params) const
{
    QRegion dirtyRegion;
    if (!params.currentMagnifierRect.isEmpty()) {
        dirtyRegion += params.currentMagnifierRect;
    }
    if (!params.lastMagnifierRect.isEmpty()) {
        dirtyRegion += params.lastMagnifierRect;
    }
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
