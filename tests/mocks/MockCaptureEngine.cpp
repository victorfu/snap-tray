#include "MockCaptureEngine.h"

MockCaptureEngine::MockCaptureEngine(QObject *parent)
    : ICaptureEngine(parent)
{
    // Create a default test frame (100x100 red image)
    m_nextFrame = QImage(100, 100, QImage::Format_ARGB32);
    m_nextFrame.fill(Qt::red);
}

bool MockCaptureEngine::setRegion(const QRect &region, QScreen *screen)
{
    m_setRegionCalls++;
    m_lastRegion = region;
    m_lastScreen = screen;
    m_captureRegion = region;
    m_targetScreen = screen;

    if (!m_regionSucceeds) {
        emit error(QStringLiteral("Mock setRegion failure"));
        return false;
    }

    return true;
}

bool MockCaptureEngine::start()
{
    m_startCalls++;

    if (!m_startSucceeds) {
        emit error(QStringLiteral("Mock start failure"));
        return false;
    }

    m_running = true;
    return true;
}

void MockCaptureEngine::stop()
{
    m_stopCalls++;
    m_running = false;
}

bool MockCaptureEngine::isRunning() const
{
    return m_running;
}

QImage MockCaptureEngine::captureFrame()
{
    m_captureCalls++;

    if (!m_running) {
        return QImage();
    }

    // Return from sequence if available
    if (!m_frameSequence.isEmpty()) {
        if (m_frameSequenceIndex < m_frameSequence.size()) {
            return m_frameSequence.at(m_frameSequenceIndex++);
        }
        // Loop back to beginning
        m_frameSequenceIndex = 0;
        return m_frameSequence.first();
    }

    return m_nextFrame;
}

void MockCaptureEngine::setNextFrame(const QImage &frame)
{
    m_nextFrame = frame;
    m_frameSequence.clear();
    m_frameSequenceIndex = 0;
}

void MockCaptureEngine::setFrameSequence(const QList<QImage> &frames)
{
    m_frameSequence = frames;
    m_frameSequenceIndex = 0;
}

void MockCaptureEngine::setStartSucceeds(bool succeeds)
{
    m_startSucceeds = succeeds;
}

void MockCaptureEngine::setRegionSucceeds(bool succeeds)
{
    m_regionSucceeds = succeeds;
}

void MockCaptureEngine::simulateError(const QString &message)
{
    emit error(message);
}

void MockCaptureEngine::simulateWarning(const QString &message)
{
    emit warning(message);
}

void MockCaptureEngine::simulateFrameReady(const QImage &frame, qint64 timestamp)
{
    emit frameReady(frame, timestamp);
}

void MockCaptureEngine::resetCounters()
{
    m_startCalls = 0;
    m_stopCalls = 0;
    m_captureCalls = 0;
    m_setRegionCalls = 0;
    m_frameSequenceIndex = 0;
}
