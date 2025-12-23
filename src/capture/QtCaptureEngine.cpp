#include "capture/QtCaptureEngine.h"

#include <QScreen>
#include <QPixmap>
#include <QDebug>

QtCaptureEngine::QtCaptureEngine(QObject *parent)
    : ICaptureEngine(parent)
{
}

QtCaptureEngine::~QtCaptureEngine()
{
    stop();
}

bool QtCaptureEngine::setRegion(const QRect &region, QScreen *screen)
{
    if (!screen) {
        emit error("Invalid screen");
        return false;
    }

    m_captureRegion = region;
    m_targetScreen = screen;
    return true;
}

bool QtCaptureEngine::start()
{
    if (!m_targetScreen) {
        emit error("No target screen configured");
        return false;
    }

    if (m_captureRegion.isEmpty()) {
        emit error("No capture region configured");
        return false;
    }

    m_running = true;
    qDebug() << "QtCaptureEngine: Started capturing region" << m_captureRegion;
    return true;
}

void QtCaptureEngine::stop()
{
    if (m_running) {
        m_running = false;
        qDebug() << "QtCaptureEngine: Stopped";
    }
}

bool QtCaptureEngine::isRunning() const
{
    return m_running;
}

QImage QtCaptureEngine::captureFrame()
{
    if (!m_running || !m_targetScreen) {
        return QImage();
    }

    // Calculate region relative to screen geometry
    QRect screenGeom = m_targetScreen->geometry();
    int relativeX = m_captureRegion.x() - screenGeom.x();
    int relativeY = m_captureRegion.y() - screenGeom.y();

    // Capture the region using Qt's screen grab
    QPixmap pixmap = m_targetScreen->grabWindow(
        0,  // Window ID 0 = entire screen
        relativeX,
        relativeY,
        m_captureRegion.width(),
        m_captureRegion.height()
    );

    if (pixmap.isNull()) {
        emit error("Failed to capture frame");
        return QImage();
    }

    return pixmap.toImage();
}
