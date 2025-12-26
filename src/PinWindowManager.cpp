#include "PinWindowManager.h"
#include "PinWindow.h"

#include <QDebug>
#include <QCursor>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QWidget>

PinWindowManager::PinWindowManager(QObject *parent)
    : QObject(parent)
{
}

PinWindowManager::~PinWindowManager()
{
    closeAllWindows();
}

PinWindow* PinWindowManager::createPinWindow(const QPixmap &screenshot, const QPoint &position)
{
    PinWindow *window = new PinWindow(screenshot, position);
    window->setPinWindowManager(this);

    connect(window, &PinWindow::closed, this, &PinWindowManager::onWindowClosed);
    connect(window, &PinWindow::ocrCompleted, this, &PinWindowManager::ocrCompleted);

    m_windows.append(window);
    // Note: show() is called in PinWindow constructor

    qDebug() << "PinWindowManager: Created window, total count:" << m_windows.count();

    emit windowCreated(window);
    return window;
}

void PinWindowManager::closeAllWindows()
{
    qDebug() << "PinWindowManager: Closing all" << m_windows.count() << "windows";

    // Make a copy of the list since closing will modify it via onWindowClosed
    QList<PinWindow*> windowsCopy = m_windows;
    for (PinWindow *window : windowsCopy) {
        window->close();
    }
}

void PinWindowManager::disableClickThroughAll()
{
    for (PinWindow *window : m_windows) {
        if (window->isClickThrough()) {
            window->setClickThrough(false);
        }
    }
}

void PinWindowManager::toggleClickThroughAtCursor()
{
    // Get the current cursor position
    QPoint cursorPos = QCursor::pos();
    
    // First, try to find the widget at the cursor position
    // This correctly handles window stacking order
    QWidget *initialWidgetAtCursor = QApplication::widgetAt(cursorPos);
    QWidget *widgetAtCursor = initialWidgetAtCursor;
    
    // Check if the widget is or belongs to a PinWindow
    while (widgetAtCursor) {
        PinWindow *pinWindow = qobject_cast<PinWindow*>(widgetAtCursor);
        if (pinWindow && m_windows.contains(pinWindow)) {
            // Toggle click-through mode for this window
            pinWindow->setClickThrough(!pinWindow->isClickThrough());
            qDebug() << "PinWindowManager: Toggled click-through for window at cursor, now"
                     << (pinWindow->isClickThrough() ? "enabled" : "disabled");
            return;
        }
        widgetAtCursor = widgetAtCursor->parentWidget();
    }
    
    // Only use fallback if widgetAt() returned null (likely a click-through window)
    // If it returned a widget, that means the cursor is over something else, not a PinWindow
    if (initialWidgetAtCursor != nullptr) {
        qDebug() << "PinWindowManager: Cursor is over another window, not a pin window";
        return;
    }
    
    // Fallback: If widgetAt() returned null (e.g., due to click-through mode),
    // check geometry manually. Iterate in reverse order to check most recently
    // created/raised windows first, approximating Z-order for better UX when
    // windows overlap.
    for (int i = m_windows.count() - 1; i >= 0; --i) {
        PinWindow *window = m_windows[i];
        
        // Check if the cursor is within the window's geometry and window is visible
        if (window->isVisible() && window->geometry().contains(cursorPos)) {
            // Toggle click-through mode for this window
            window->setClickThrough(!window->isClickThrough());
            qDebug() << "PinWindowManager: Toggled click-through for window at cursor (fallback), now"
                      << (window->isClickThrough() ? "enabled" : "disabled");
            return;
        }
    }
    
    qDebug() << "PinWindowManager: No pin window found at cursor position";
}

void PinWindowManager::onWindowClosed(PinWindow *window)
{
    m_windows.removeOne(window);

    qDebug() << "PinWindowManager: Window closed, remaining:" << m_windows.count();

    emit windowClosed(window);

    if (m_windows.isEmpty()) {
        emit allWindowsClosed();
    }
}
