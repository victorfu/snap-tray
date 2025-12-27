#include "PinWindowManager.h"
#include "PinWindow.h"

#include <QDebug>
#include <QHotkey>

PinWindowManager::PinWindowManager(QObject *parent)
    : QObject(parent)
{
}

PinWindowManager::~PinWindowManager()
{
    // Clean up click-through hotkey if it exists
    if (m_clickThroughHotkey) {
        m_clickThroughHotkey->setRegistered(false);
        delete m_clickThroughHotkey;
        m_clickThroughHotkey = nullptr;
    }
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

void PinWindowManager::onClickThroughChanged()
{
    updateClickThroughHotkey();
}

void PinWindowManager::updateClickThroughHotkey()
{
    // Check if any window is in click-through mode
    bool anyClickThrough = false;
    for (PinWindow *window : m_windows) {
        if (window->isClickThrough()) {
            anyClickThrough = true;
            break;
        }
    }

    if (anyClickThrough) {
        // Register hotkey if not already registered
        if (!m_clickThroughHotkey) {
            m_clickThroughHotkey = new QHotkey(QKeySequence(Qt::Key_T), true, this);
            connect(m_clickThroughHotkey, &QHotkey::activated, this, &PinWindowManager::disableClickThroughAll);
            
            if (m_clickThroughHotkey->isRegistered()) {
                qDebug() << "PinWindowManager: Click-through hotkey registered";
            } else {
                qDebug() << "PinWindowManager: Failed to register click-through hotkey";
            }
        }
    } else {
        // Unregister hotkey if no windows are in click-through mode
        if (m_clickThroughHotkey) {
            m_clickThroughHotkey->setRegistered(false);
            delete m_clickThroughHotkey;
            m_clickThroughHotkey = nullptr;
            qDebug() << "PinWindowManager: Click-through hotkey unregistered";
        }
    }
}

void PinWindowManager::onWindowClosed(PinWindow *window)
{
    m_windows.removeOne(window);

    qDebug() << "PinWindowManager: Window closed, remaining:" << m_windows.count();

    // Update hotkey in case the closed window was in click-through mode
    updateClickThroughHotkey();

    emit windowClosed(window);

    if (m_windows.isEmpty()) {
        emit allWindowsClosed();
    }
}
