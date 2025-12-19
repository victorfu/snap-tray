#include "ScreenCanvasManager.h"
#include "ScreenCanvas.h"

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

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

#ifdef Q_OS_MACOS
        // 設定視窗層級高於 menu bar (menu bar 在 layer 25)
        NSView* view = reinterpret_cast<NSView*>(m_canvas->winId());
        if (view) {
            NSWindow* window = [view window];
            [window setLevel:kCGScreenSaverWindowLevel];  // level 1000
        }
#endif

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
