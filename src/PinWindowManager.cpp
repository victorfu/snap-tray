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

PinWindow* PinWindowManager::createPinWindow(const QPixmap &screenshot,
                                             const QPoint &position,
                                             bool autoSaveToCache)
{
    PinWindow *window = new PinWindow(screenshot, position, nullptr, autoSaveToCache);
    window->setPinWindowManager(this);

    connect(window, &PinWindow::closed, this, &PinWindowManager::onWindowClosed);
    connect(window, &PinWindow::ocrCompleted, this, &PinWindowManager::ocrCompleted);
    connect(window, &PinWindow::saveCompleted, this, &PinWindowManager::saveCompleted);
    connect(window, &PinWindow::saveFailed, this, &PinWindowManager::saveFailed);

    m_windows.append(window);
    // Note: show() is called in PinWindow constructor

    emit windowCreated(window);
    return window;
}

void PinWindowManager::closeAllWindows()
{
    // Make a copy since window close events mutate m_windows.
    const QList<PinWindow*> windowsCopy = m_windows;
    closeWindows(windowsCopy);
}

void PinWindowManager::closeWindows(const QList<PinWindow*>& windows)
{
    for (PinWindow* window : windows) {
        if (window && m_windows.contains(window)) {
            window->close();
        }
    }
}

void PinWindowManager::onWindowClosed(PinWindow *window)
{
    m_windows.removeOne(window);

    emit windowClosed(window);

    if (m_windows.isEmpty()) {
        emit allWindowsClosed();
    }
}

void PinWindowManager::updateOcrLanguages(const QStringList &languages)
{
    for (PinWindow *window : m_windows) {
        if (window) {
            window->updateOcrLanguages(languages);
        }
    }
}
