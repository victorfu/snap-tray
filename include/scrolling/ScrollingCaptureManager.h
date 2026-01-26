#ifndef SCROLLINGCAPTUREMANAGER_H
#define SCROLLINGCAPTUREMANAGER_H

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QImage>
#include <QTimer>
#include <memory>
#include "scrolling/ScrollingCaptureOptions.h"

class WindowHighlighter;
class AutoScroller;
class ImageCombiner;
class QScreen;
class WindowDetector;

/**
 * @brief Main controller for scrolling capture feature
 *
 * Manages the state machine and coordinates all scrolling capture components:
 * - WindowHighlighter for window selection (with embedded Start/Cancel buttons)
 * - AutoScroller for scrolling and frame capture
 * - ImageCombiner for stitching frames
 *
 * Follows RecordingManager pattern for consistency.
 */
class ScrollingCaptureManager : public QObject
{
    Q_OBJECT

public:
    explicit ScrollingCaptureManager(QObject *parent = nullptr);
    ~ScrollingCaptureManager();

    /**
     * @brief Check if any capture activity is in progress
     */
    bool isActive() const;

    /**
     * @brief Check if actively capturing frames
     */
    bool isCapturing() const;

    /**
     * @brief Get current state
     */
    ScrollingCaptureState state() const { return m_state; }

public slots:
    /**
     * @brief Start window selection mode
     */
    void startWindowSelection();

    /**
     * @brief Cancel capture and return to idle
     */
    void cancelCapture();

    /**
     * @brief Stop capturing (if in progress)
     */
    void stopCapture();

signals:
    void stateChanged(ScrollingCaptureState state);
    void captureProgress(int frameCount);
    void captureCompleted(const QImage& result, CaptureStatus status);
    void captureError(const QString& error);
    void captureCancelled();

    // Save/export signals (integrate with existing export flow)
    void saveRequested(const QImage& image);
    void copyRequested(const QImage& image);
    void pinRequested(const QImage& image, const QPoint& position);

private slots:
    void onWindowListReady();
    void onWindowHovered(WId windowId, const QString& title);
    void onStartClicked();
    void onCancelClicked();
    void onStopClicked();

    // AutoScroller callbacks
    void onFrameCaptured(int frameIndex);
    void onScrollingCompleted(CaptureStatus status);
    void onScrollingError(const QString& error);

private:
    void setState(ScrollingCaptureState newState);
    void cleanup();
    void startCapturing();
    void finishCapture(CaptureStatus status);
    void showResult();

    ScrollingCaptureState m_state = ScrollingCaptureState::Idle;
    ScrollingCaptureOptions m_options;

    // UI Components
    QPointer<WindowHighlighter> m_highlighter;

    // Capture components
    std::unique_ptr<AutoScroller> m_autoScroller;
    std::unique_ptr<ImageCombiner> m_imageCombiner;
    std::unique_ptr<WindowDetector> m_windowDetector;

    // Target window info
    WId m_targetWindow = 0;
    QRect m_targetRect;
    QScreen* m_targetScreen = nullptr;

    // Result
    QImage m_resultImage;
    CaptureStatus m_resultStatus = CaptureStatus::Failed;
};

#endif // SCROLLINGCAPTUREMANAGER_H
