#pragma once

#include <QRect>
#include <QSet>
#include <QVariantList>

#include <algorithm>

#include "tools/ToolbarTraits.h"

namespace SnapTray {
struct ToolbarLayoutMetrics {
    int buttonWidth = 28;
    int buttonSpacing = 2;
    int separatorWidth = 8;
    int margin = 8;
    int dragHandleWidth = 16;
    int contentSpacing = 6;
    int groupGap = 4;   // extra space at a group boundary, on top of the separator
};
struct ToolbarOverflowLayout {
    QVariantList visibleButtons;
    QVariantList overflowButtons;
    bool showDragHandle = true;
    int width = 0;
};

inline QRect toolbarUsableBounds(const QRect& screen)
{
    if (screen.isEmpty()) return {};
    const int mx = qMin(10, (screen.width() - 1) / 2);
    const int my = qMin(10, (screen.height() - 1) / 2);
    return screen.adjusted(mx, my, -mx, -my);
}

inline QPoint boundToolbarPosition(const QPoint& desired, const QSize& size, const QRect& bounds)
{
    if (bounds.isEmpty()) return {};
    return QPoint(qBound(bounds.x(), desired.x(), bounds.x() + qMax(0, bounds.width() - size.width())),
                  qBound(bounds.y(), desired.y(), bounds.y() + qMax(0, bounds.height() - size.height())));
}

inline int toolbarContentWidth(const QVariantList& buttons, bool drag, bool more,
                                const ToolbarLayoutMetrics& m)
{
    int width = 2 * m.margin;
    if (drag) width += m.dragHandleWidth + m.contentSpacing;
    for (const QVariant& button : buttons) {
        const QVariantMap map = button.toMap();
        width += m.buttonWidth;
        if (map.value("separatorBefore").toBool()) width += m.separatorWidth;
        if (map.value("groupStart").toBool()) width += m.groupGap;
    }
    const int count = int(buttons.size()) + (more ? 1 : 0);
    if (more) width += m.buttonWidth;
    return width + qMax(0, count - 1) * m.buttonSpacing;
}

// Re-derives group boundaries after buttons were removed from a row.
inline QVariantList restampGroupStarts(QVariantList list)
{
    int previous = -1;
    for (auto& v : list) {
        QVariantMap m = v.toMap();
        const int g = m.value("group").toInt();
        const bool start = previous != -1 && g != previous;
        m["groupStart"] = start;
        if (start) m["separatorBefore"] = true;
        v = m;
        previous = g;
    }
    return list;
}

// Collapses buttons into More by collapsePriority (lowest first, later in the
// row first on ties). kNeverCollapse buttons, the active tool and
// `keepToolId` (the tool in use when the highlight is elsewhere) stay visible.
inline ToolbarOverflowLayout computeToolbarOverflow(const QVariantList& buttons, int maximumWidth,
                                                     int activeToolId,
                                                     const ToolbarLayoutMetrics& metrics = {},
                                                     int keepToolId = -1)
{
    ToolbarOverflowLayout result;
    result.visibleButtons = buttons;
    result.width = toolbarContentWidth(buttons, true, false, metrics);
    if (result.width <= maximumWidth) return result;

    QList<int> order;
    for (int i = 0; i < buttons.size(); ++i) {
        const QVariantMap b = buttons[i].toMap();
        if (b.value("collapsePriority").toInt() == kNeverCollapse) continue;
        const int id = b.value("id").toInt();
        if (id == activeToolId || (keepToolId >= 0 && id == keepToolId)) continue;
        order.append(i);
    }
    std::stable_sort(order.begin(), order.end(), [&buttons](int a, int b) {
        const int pa = buttons[a].toMap().value("collapsePriority").toInt();
        const int pb = buttons[b].toMap().value("collapsePriority").toInt();
        return pa != pb ? pa < pb : a > b;
    });

    QSet<int> removed;
    for (int index : order) {
        removed.insert(index);
        QVariantList visible;
        for (int i = 0; i < buttons.size(); ++i)
            if (!removed.contains(i)) visible.append(buttons[i]);
        result.visibleButtons = restampGroupStarts(visible);
        result.width = toolbarContentWidth(result.visibleButtons, true, true, metrics);
        if (result.width <= maximumWidth) break;
    }
    if (result.width > maximumWidth) {
        result.visibleButtons.clear();
        result.overflowButtons = buttons;
        result.showDragHandle = false;
        result.width = qMax(1, qMin(maximumWidth, toolbarContentWidth({}, false, true, metrics)));
        return result;
    }
    for (int i = 0; i < buttons.size(); ++i)
        if (removed.contains(i)) result.overflowButtons.append(buttons[i]);
    return result;
}
}
