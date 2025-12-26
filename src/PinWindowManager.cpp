#include "PinWindowManager.h"
#include "PinWindow.h"

#include <QDebug>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

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
    
    // Find the topmost pin window at the cursor position
    // We need to check from the end of the list (most recently created/raised)
    // to get the topmost window
    for (int i = m_windows.count() - 1; i >= 0; --i) {
        PinWindow *window = m_windows[i];
        
        // Check if the cursor is within the window's geometry
        if (window->geometry().contains(cursorPos)) {
            // Toggle click-through mode for this window
            window->setClickThrough(!window->isClickThrough());
            qDebug() << "PinWindowManager: Toggled click-through for window at cursor, now"
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
