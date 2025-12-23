#include "FrameProcessorThread.h"
#include "capture/ICaptureEngine.h"
#include "FFmpegEncoder.h"

#include <QDebug>
#include <QPixmap>

FrameProcessorThread::FrameProcessorThread(QObject *parent)
    : QThread(parent)
{
}

FrameProcessorThread::~FrameProcessorThread()
{
    requestStop();
    if (!wait(5000)) {
        qWarning() << "FrameProcessorThread: Force terminating after timeout";
        terminate();
        wait();
    }
}

void FrameProcessorThread::initialize(ICaptureEngine *captureEngine, FFmpegEncoder *encoder)
{
    m_captureEngine = captureEngine;
    m_encoder = encoder;
}

bool FrameProcessorThread::requestCapture()
{
    QMutexLocker locker(&m_mutex);

    // Backpressure: skip frame if queue is too deep
    if (m_pendingCaptures >= MAX_PENDING_CAPTURES) {
        m_skippedCount++;
        // Emit signal outside of lock to avoid deadlock
        locker.unlock();
        emit frameSkipped();
        return false;
    }

    m_pendingCaptures++;
    m_condition.wakeOne();
    return true;
}

int FrameProcessorThread::pendingCaptureCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_pendingCaptures;
}

void FrameProcessorThread::requestStop()
{
    m_stopRequested = true;
    m_condition.wakeAll();
}

void FrameProcessorThread::run()
{
    qDebug() << "FrameProcessorThread: Started on thread" << QThread::currentThreadId();
    m_processing = true;

    while (!m_stopRequested) {
        {
            QMutexLocker locker(&m_mutex);

            // Wait for capture request or stop signal
            while (m_pendingCaptures == 0 && !m_stopRequested) {
                m_condition.wait(&m_mutex);
            }

            if (m_stopRequested) {
                break;
            }

            // Decrement pending count before processing
            m_pendingCaptures--;
        }

        processNextCapture();
    }

    m_processing = false;
    qDebug() << "FrameProcessorThread: Stopped, total frames:" << m_frameCount.load()
             << "skipped:" << m_skippedCount.load();
}

void FrameProcessorThread::processNextCapture()
{
    if (!m_captureEngine || !m_encoder) {
        emit processingError("Capture engine or encoder not initialized");
        return;
    }

    // Capture frame (may block 5-50ms for GPU readback on DXGI)
    QImage frame = m_captureEngine->captureFrame();

    if (frame.isNull()) {
        // Capture failed - this can happen transiently, don't emit error
        // The capture engine may return null if no new frame is available
        return;
    }

    // Convert QImage to QPixmap and encode
    // This conversion and pipe write may block 10-100ms
    QPixmap pixmap = QPixmap::fromImage(frame);
    m_encoder->writeFrame(pixmap);

    qint64 count = ++m_frameCount;
    emit frameProcessed(count);
}
