#ifndef AUTOSCROLLER_H
#define AUTOSCROLLER_H

#include <QObject>
#include <QImage>
#include <QRect>
#include <QTimer>
#include <memory>
#include "scrolling/ScrollingCaptureOptions.h"

class ICaptureEngine;
class ImageCombiner;
class ScrollSender;

/**
 * @brief Handles automatic scrolling and frame capture
 *
 * Implements the capture loop:
 * 1. Capture current frame
 * 2. Combine with accumulated result
 * 3. Check if reached bottom
 * 4. Send scroll event
 * 5. Wait for content to settle
 * 6. Repeat until done
 */
class AutoScroller : public QObject
{
    Q_OBJECT

public:
    explicit AutoScroller(QObject *parent = nullptr);
    ~AutoScroller();

    /**
     * @brief Configure capture options
     */
    void setOptions(const ScrollingCaptureOptions& options);

    /**
     * @brief Set target window and region
     */
    void setTarget(WId window, const QRect& rect);

    /**
     * @brief Set image combiner for stitching
     */
    void setImageCombiner(ImageCombiner* combiner);

    /**
     * @brief Start capture loop
     */
    void start();

    /**
     * @brief Stop capture loop
     */
    void stop(bool emitCompletion = false);

    /**
     * @brief Check if running
     */
    bool isRunning() const { return m_running; }

signals:
    /**
     * @brief Emitted when a frame is captured
     */
    void frameCaptured(int frameIndex);

    /**
     * @brief Emitted with progress updates
     */
    void progressUpdated(int frameCount, int estimatedHeight);

    /**
     * @brief Emitted when capture completes
     */
    void completed(CaptureStatus status);

    /**
     * @brief Emitted on error
     */
    void error(const QString& message);

private slots:
    void captureLoop();

private:
    bool initializeCaptureEngine();
    void moveCursorToTarget();
    bool scrollToTop();
    bool sendScroll();
    QImage captureCurrentFrame();
    bool isScrollReachedBottom(const QImage& prevFrame, const QImage& currFrame);
    bool compareImages(const QImage& a, const QImage& b);
    void cleanupResources();
    void emitCompletedOnce();

    ScrollingCaptureOptions m_options;
    WId m_targetWindow = 0;
    QRect m_targetRect;

    std::unique_ptr<ICaptureEngine> m_captureEngine;
    std::unique_ptr<ScrollSender> m_scrollSender;
    ImageCombiner* m_imageCombiner = nullptr;

    QTimer* m_loopTimer = nullptr;
    QImage m_previousFrame;
    bool m_running = false;
    bool m_stopRequested = false;
    bool m_inCaptureLoop = false;
    bool m_completionEmitted = false;
    bool m_emitCompletionOnStop = false;
    int m_frameCount = 0;
    CaptureStatus m_overallStatus = CaptureStatus::Successful;
};

#endif // AUTOSCROLLER_H
