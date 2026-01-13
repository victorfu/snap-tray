#include "PinWindowManager.h"
#include "PinWindow.h"

#include <QDebug>

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
    connect(window, &PinWindow::saveCompleted, this, &PinWindowManager::saveCompleted);
    connect(window, &PinWindow::saveFailed, this, &PinWindowManager::saveFailed);

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

void PinWindowManager::onWindowClosed(PinWindow *window)
{
    m_windows.removeOne(window);

    qDebug() << "PinWindowManager: Window closed, remaining:" << m_windows.count();

    emit windowClosed(window);

    if (m_windows.isEmpty()) {
        emit allWindowsClosed();
    }
}
