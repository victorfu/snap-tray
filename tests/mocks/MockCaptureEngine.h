#ifndef MOCKCAPTUREENGINE_H
#define MOCKCAPTUREENGINE_H

#include "capture/ICaptureEngine.h"
#include <QImage>

/**
 * @brief Mock implementation of ICaptureEngine for testing
 *
 * Allows controlling capture behavior and tracking method calls
 * without actual screen capture.
 */
class MockCaptureEngine : public ICaptureEngine
{
    Q_OBJECT

public:
    explicit MockCaptureEngine(QObject *parent = nullptr);
    ~MockCaptureEngine() override = default;

    // ICaptureEngine interface implementation
    bool setRegion(const QRect &region, QScreen *screen) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QImage captureFrame() override;
    QString engineName() const override { return QStringLiteral("MockCaptureEngine"); }

    // ========== Mock Control Methods ==========

    /**
     * @brief Set the image returned by captureFrame()
     */
    void setNextFrame(const QImage &frame);

    /**
     * @brief Set a list of frames to return sequentially
     */
    void setFrameSequence(const QList<QImage> &frames);

    /**
     * @brief Control whether start() succeeds
     */
    void setStartSucceeds(bool succeeds);

    /**
     * @brief Control whether setRegion() succeeds
     */
    void setRegionSucceeds(bool succeeds);

    /**
     * @brief Emit an error signal
     */
    void simulateError(const QString &message);

    /**
     * @brief Emit a warning signal
     */
    void simulateWarning(const QString &message);

    /**
     * @brief Emit frameReady signal asynchronously
     */
    void simulateFrameReady(const QImage &frame, qint64 timestamp);

    // ========== Spy Methods ==========

    int startCallCount() const { return m_startCalls; }
    int stopCallCount() const { return m_stopCalls; }
    int captureFrameCallCount() const { return m_captureCalls; }
    int setRegionCallCount() const { return m_setRegionCalls; }
    QRect lastRegion() const { return m_lastRegion; }
    QScreen* lastScreen() const { return m_lastScreen; }

    /**
     * @brief Reset all call counters
     */
    void resetCounters();

private:
    QImage m_nextFrame;
    QList<QImage> m_frameSequence;
    int m_frameSequenceIndex = 0;
    bool m_running = false;
    bool m_startSucceeds = true;
    bool m_regionSucceeds = true;

    // Call counters
    int m_startCalls = 0;
    int m_stopCalls = 0;
    int m_captureCalls = 0;
    int m_setRegionCalls = 0;

    // Last call parameters
    QRect m_lastRegion;
    QScreen *m_lastScreen = nullptr;
};

#endif // MOCKCAPTUREENGINE_H
