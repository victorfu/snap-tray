#ifndef WINDOWINTERACTIONUTILS_H
#define WINDOWINTERACTIONUTILS_H

#include <QPoint>

class QWidget;

namespace Toolbar {

/**
 * Shared outside-click guard for floating toolbar windows.
 * Returns true when the click should NOT close the toolbar.
 */
bool shouldIgnoreOutsideClick(const QPoint& globalPos,
                              const QWidget* toolbarWindow,
                              const QWidget* subToolbarWindow,
                              const QWidget* hostWindow);

} // namespace Toolbar

#endif // WINDOWINTERACTIONUTILS_H
