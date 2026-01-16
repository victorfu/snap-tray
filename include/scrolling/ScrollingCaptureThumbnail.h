#ifndef SCROLLINGCAPTURETHUMBNAIL_H
#define SCROLLINGCAPTURETHUMBNAIL_H

#include <QWidget>
#include <QImage>
#include <QElapsedTimer>
#include <QTimer>
#include <QMap>
#include <QLabel>
#include <QVBoxLayout>

#include "scrolling/ImageStitcher.h"

class ScrollingCaptureThumbnail : public QWidget
{
    Q_OBJECT

public:
    enum class CaptureStatus {
        Idle,       // Not capturing
        Capturing,  // Active capture (green)
        Warning,    // Recoverable issue (yellow)
        Failed,     // Needs user action (red)
        Recovered   // Transitioning from Failed to Capturing (blue, 1.5s)
    };
    Q_ENUM(CaptureStatus)

    enum class CaptureDirection {
        Vertical,
        Horizontal
    };
    Q_ENUM(CaptureDirection)

    enum class ScrollSpeed {
        Idle,       // Not capturing
        TooSlow,    // Overlap > 80% - could scroll faster
        Good,       // Overlap 30-70% - optimal range
        TooFast     // Overlap < 30% - needs to slow down
    };
    Q_ENUM(ScrollSpeed)

    explicit ScrollingCaptureThumbnail(QWidget *parent = nullptr);
    ~ScrollingCaptureThumbnail();

    void setViewportImage(const QImage& image);
    void setStats(int frameCount, QSize totalSize);
    void setStatus(CaptureStatus status, const QString& message = QString());
    void setErrorInfo(ImageStitcher::FailureCode code, const QString& debugReason, int recoveryDistancePx);
    void clearError();
    void setCaptureDirection(CaptureDirection direction);
    void setOverlapRatio(double ratio);  // Updates scroll speed indicator (0.0-1.0)
    void positionNear(const QRect &captureRegion);

    // Compatibility method for existing code that uses Direction
    using Direction = CaptureDirection;
    void setDirection(Direction direction) { setCaptureDirection(direction); }
    
    enum class MatchStatus {
        Good,
        Warning,
        Failed
    };
    Q_ENUM(MatchStatus)

    // Compatibility method for existing code
    void setMatchStatus(MatchStatus status, double confidence);


protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct ErrorInfo {
        QString title;
        QString hint;
    };

    void setupUI();
    void applyThemeColors();
    void applyViewportImage(const QImage& image);
    void applyStatus(CaptureStatus status, const QString& message);
    void applyScrollSpeed(ScrollSpeed speed);
    ErrorInfo failureCodeToErrorInfo(ImageStitcher::FailureCode code, int recoveryPx) const;

    // UI Components
    QLabel* m_viewportLabel;
    QLabel* m_statusLabel;      // "● Capturing"
    QLabel* m_statsLabel;       // "45 frames • 800 x 12000 px"
    QLabel* m_speedLabel;       // Scroll speed indicator
    QWidget* m_errorSection;
    QLabel* m_errorTitleLabel;
    QLabel* m_errorHintLabel;
    
    // Throttling
    QElapsedTimer m_lastViewportUpdate;
    static constexpr int VIEWPORT_UPDATE_INTERVAL_MS = 100;
    QImage m_pendingViewportImage;
    QTimer* m_viewportThrottleTimer = nullptr;
    
    // Status management
    QTimer* m_recoveredTimer = nullptr;
    CaptureStatus m_pendingStatus = CaptureStatus::Idle;
    CaptureStatus m_currentStatus = CaptureStatus::Idle;
    
    CaptureDirection m_captureDirection = CaptureDirection::Vertical;
    ScrollSpeed m_scrollSpeed = ScrollSpeed::Idle;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging = false;
};

#endif // SCROLLINGCAPTURETHUMBNAIL_H