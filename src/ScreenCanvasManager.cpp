#include "ScreenCanvasManager.h"
#include "ScreenCanvas.h"
#include "platform/WindowLevel.h"

#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QDebug>

ScreenCanvasManager::ScreenCanvasManager(QObject *parent)
    : QObject(parent)
    , m_canvas(nullptr)
{
    qDebug() << "ScreenCanvasManager: Created";
}

ScreenCanvasManager::~ScreenCanvasManager()
{
    if (m_canvas) {
        m_canvas->close();
    }
    qDebug() << "ScreenCanvasManager: Destroyed";
}

bool ScreenCanvasManager::isActive() const
{
    return m_canvas && m_canvas->isVisible();
}

void ScreenCanvasManager::toggle()
{
    if (m_canvas && m_canvas->isVisible()) {
        // Canvas is active - close it
        qDebug() << "ScreenCanvasManager: Closing canvas via toggle";
        m_canvas->close();
        // m_canvas will be nulled via QPointer after deleteLater
    } else {
        // No canvas - create and show one
        QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
        if (!targetScreen) {
            targetScreen = QGuiApplication::primaryScreen();
        }

        qDebug() << "ScreenCanvasManager: Opening canvas on screen" << targetScreen->name();

        m_canvas = new ScreenCanvas();
        m_canvas->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_canvas, &ScreenCanvas::closed, this, &ScreenCanvasManager::onCanvasClosed);

        m_canvas->initializeForScreen(targetScreen);
        m_canvas->setGeometry(targetScreen->geometry());
        m_canvas->show();
        raiseWindowAboveMenuBar(m_canvas);
        setWindowClickThrough(m_canvas, false);
        m_canvas->activateWindow();
        m_canvas->raise();

        emit canvasOpened();
    }
}

void ScreenCanvasManager::onCanvasClosed()
{
    qDebug() << "ScreenCanvasManager: Canvas closed";
    emit canvasClosed();
}
