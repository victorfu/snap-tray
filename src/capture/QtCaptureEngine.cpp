#include "capture/QtCaptureEngine.h"

#include <QScreen>
#include <QGuiApplication>
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
    m_screenInfo = {};
    return true;
}

bool QtCaptureEngine::setRegion(const QRect &region, const CaptureScreenInfo &screenInfo)
{
    if (!screenInfo.isValid()) {
        emit error("Invalid screen metadata");
        return false;
    }

    m_captureRegion = region;
    m_screenInfo = screenInfo;
    m_targetScreen = nullptr;
    return true;
}

bool QtCaptureEngine::start()
{
    if (!m_targetScreen && !m_screenInfo.isValid()) {
        emit error("No target screen configured");
        return false;
    }

    if (m_captureRegion.isEmpty() || m_captureRegion.width() < 10 || m_captureRegion.height() < 10) {
        emit error("Capture region too small (minimum 10x10 pixels)");
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
    if (!m_running) {
        return QImage();
    }

    QScreen *targetScreen = m_screenInfo.isValid() ? resolveTargetScreen() : m_targetScreen;
    if (!targetScreen) {
        emit error("Target screen is no longer available");
        return QImage();
    }

    // Calculate region relative to screen geometry (logical coordinates)
    const QRect screenGeom = m_screenInfo.isValid()
        ? m_screenInfo.geometry
        : targetScreen->geometry();
    int relativeX = m_captureRegion.x() - screenGeom.x();
    int relativeY = m_captureRegion.y() - screenGeom.y();

    // grabWindow() takes all parameters in device-independent pixels (logical coordinates)
    // Qt handles DPI scaling internally; returned pixmap may be larger on HiDPI displays
    QPixmap pixmap = targetScreen->grabWindow(
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

QScreen *QtCaptureEngine::resolveTargetScreen() const
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        if (!m_screenInfo.name.isEmpty() && screen->name() == m_screenInfo.name) {
            return screen;
        }
    }
    for (QScreen *screen : screens) {
        if (screen->geometry() == m_screenInfo.geometry) {
            return screen;
        }
    }
    return nullptr;
}
