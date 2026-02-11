#include "toolbar/WindowInteractionUtils.h"

#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <QWidget>

namespace {

bool containsGlobalPoint(const QWidget* widget, const QPoint& globalPos)
{
    return widget && widget->isVisible() && widget->frameGeometry().contains(globalPos);
}

bool isTransientPopup(const QWidget* widget)
{
    return widget &&
           (qobject_cast<const QDialog*>(widget) ||
            qobject_cast<const QMenu*>(widget) ||
            widget->windowFlags().testFlag(Qt::Popup));
}

} // namespace

namespace Toolbar {

bool shouldIgnoreOutsideClick(const QPoint& globalPos,
                              const QWidget* toolbarWindow,
                              const QWidget* subToolbarWindow,
                              const QWidget* hostWindow)
{
    if (containsGlobalPoint(toolbarWindow, globalPos)) {
        return true;
    }

    if (containsGlobalPoint(subToolbarWindow, globalPos)) {
        return true;
    }

    if (containsGlobalPoint(hostWindow, globalPos)) {
        return true;
    }

    if (const QWidget* popup = QApplication::activePopupWidget();
        containsGlobalPoint(popup, globalPos)) {
        return true;
    }

    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (const QWidget* widget : topLevelWidgets) {
        if (!isTransientPopup(widget)) {
            continue;
        }
        if (containsGlobalPoint(widget, globalPos)) {
            return true;
        }
    }

    return false;
}

} // namespace Toolbar
