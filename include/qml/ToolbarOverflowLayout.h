#pragma once

#include <QRect>
#include <QSet>
#include <QVariantList>

namespace SnapTray {
struct ToolbarLayoutMetrics {
    int buttonWidth = 28;
    int buttonSpacing = 2;
    int separatorWidth = 8;
    int margin = 8;
    int dragHandleWidth = 16;
    int contentSpacing = 6;
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
        width += m.buttonWidth;
        if (button.toMap().value("separatorBefore").toBool()) width += m.separatorWidth;
    }
    const int count = int(buttons.size()) + (more ? 1 : 0);
    if (more) width += m.buttonWidth;
    return width + qMax(0, count - 1) * m.buttonSpacing;
}

inline ToolbarOverflowLayout computeToolbarOverflow(const QVariantList& buttons, int maximumWidth,
                                                     const QSet<int>& pinned,
                                                     const ToolbarLayoutMetrics& metrics = {})
{
    ToolbarOverflowLayout result;
    result.visibleButtons = buttons;
    result.width = toolbarContentWidth(buttons, true, false, metrics);
    if (result.width <= maximumWidth) return result;
    QSet<int> removed;
    for (int i = int(buttons.size()) - 1; i >= 0; --i) {
        const int id = buttons[i].toMap().value("id").toInt();
        if (pinned.contains(id)) continue;
        removed.insert(id);
        result.visibleButtons.removeAt(i);
        result.width = toolbarContentWidth(result.visibleButtons, true, true, metrics);
        if (result.width <= maximumWidth) break;
    }
    if (result.width > maximumWidth) {
        result.visibleButtons.clear();
        result.overflowButtons = buttons;
        result.showDragHandle = false;
        result.width = qMax(1, qMin(maximumWidth, toolbarContentWidth({}, false, true, metrics)));
    } else {
        for (const QVariant& button : buttons)
            if (removed.contains(button.toMap().value("id").toInt()))
                result.overflowButtons.append(button);
    }
    return result;
}
}
