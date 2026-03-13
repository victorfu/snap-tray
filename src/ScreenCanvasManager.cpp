#include "ScreenCanvasManager.h"

#include "ScreenCanvasSession.h"

#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

ScreenCanvasManager::ScreenCanvasManager(QObject* parent)
    : QObject(parent)
{
    qDebug() << "ScreenCanvasManager: Created";
}

ScreenCanvasManager::~ScreenCanvasManager()
{
    if (m_session) {
        m_session->close();
    }
    qDebug() << "ScreenCanvasManager: Destroyed";
}

bool ScreenCanvasManager::isActive() const
{
    return m_session && m_session->isOpen();
}

void ScreenCanvasManager::toggle()
{
    if (m_session && m_session->isOpen()) {
        qDebug() << "ScreenCanvasManager: Closing canvas session via toggle";
        m_session->close();
        return;
    }

    QScreen* targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen) {
        qWarning() << "ScreenCanvasManager: No screen available";
        return;
    }

    qDebug() << "ScreenCanvasManager: Opening canvas session on screen" << targetScreen->name();

    m_session = new ScreenCanvasSession(this);
    connect(m_session, &ScreenCanvasSession::closed,
            this, &ScreenCanvasManager::onSessionClosed);
    connect(m_session, &QObject::destroyed, this, [this]() {
        m_session = nullptr;
    });

    m_session->open(targetScreen);
    if (m_session && m_session->isOpen()) {
        emit canvasOpened();
    }
}

void ScreenCanvasManager::onSessionClosed()
{
    qDebug() << "ScreenCanvasManager: Canvas session closed";
    emit canvasClosed();
}
