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

    explicit ScrollingCaptureThumbnail(QWidget *parent = nullptr);
    ~ScrollingCaptureThumbnail();

    void setViewportImage(const QImage& image);
    void setStats(int frameCount, QSize totalSize);
    void setStatus(CaptureStatus status, const QString& message = QString());
    void setErrorInfo(ImageStitcher::FailureCode code, const QString& debugReason, int recoveryDistancePx);
    void clearError();
    void setCaptureDirection(CaptureDirection direction);
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
    void setupUI();
    void applyThemeColors();
    void applyViewportImage(const QImage& image);
    void applyStatus(CaptureStatus status, const QString& message);
    QString failureCodeToUserMessage(ImageStitcher::FailureCode code);

    // UI Components
    QLabel* m_viewportLabel;
    QLabel* m_statusLabel;     // "● Capturing"
    QLabel* m_statsLabel;      // "45 frames • 800 x 12000 px"
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

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging = false;
};

#endif // SCROLLINGCAPTURETHUMBNAIL_H