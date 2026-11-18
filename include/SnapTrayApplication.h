#pragma once

#include <QApplication>
#include <QEvent>
#include <QPointer>

#include "PinWindowManager.h"

class SnapTrayApplication : public QApplication
{
public:
    using QApplication::QApplication;

    void setPinWindowManager(PinWindowManager* manager) { m_pins = manager; }

    bool notify(QObject* receiver, QEvent* event) override
    {
        // Other quit observers (including Recording Preview) may stop work or
        // schedule cleanup. Finish the pin prompts before they see Quit.
        if (receiver == this && event->type() == QEvent::Quit && m_pins
            && !m_pins->confirmApplicationQuit()) {
            event->ignore();
            return true;
        }
        return QApplication::notify(receiver, event);
    }

private:
    QPointer<PinWindowManager> m_pins;
};
