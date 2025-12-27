#ifndef SCROLLINGCAPTUREOVERLAY_H
#define SCROLLINGCAPTUREOVERLAY_H

#include <QWidget>
#include <QRect>
#include <QPixmap>
#include <QTimer>

class QScreen;
class SelectionStateManager;

class ScrollingCaptureOverlay : public QWidget
{
    Q_OBJECT

public:
    enum class BorderState {
        Selecting,      // No border - drawing selection
        Adjusting,      // Blue border - selection adjustable
        Capturing,      // Red border - capturing in progress
        MatchFailed     // Red flicker - match failed
    };
    Q_ENUM(BorderState)

    enum class CaptureDirection {
        Vertical,
        Horizontal
    };
    Q_ENUM(CaptureDirection)

    explicit ScrollingCaptureOverlay(QWidget *parent = nullptr);
    ~ScrollingCaptureOverlay();

    void initializeForScreen(QScreen *screen);
    void initializeForScreenWithRegion(QScreen *screen, const QRect &globalRegion);
    void setAllowNewSelection(bool allow);
    void setBorderState(BorderState state);
    BorderState borderState() const { return m_borderState; }

    QRect captureRegion() const;
    QScreen *screen() const { return m_screen; }

    void setRegionLocked(bool locked);
    bool isRegionLocked() const { return m_regionLocked; }

    void setCaptureDirection(CaptureDirection direction);
    CaptureDirection captureDirection() const { return m_captureDirection; }

    void setAllowMoveWhileCapturing(bool allow);
    bool allowMoveWhileCapturing() const { return m_allowMoveWhileCapturing; }

    void setMatchFailedMessage(const QString &message);
    void clearMatchFailedMessage();

signals:
    void regionSelected(const QRect &region);
    void regionChanged(const QRect &region);
    void selectionConfirmed();
    void captureRequested();
    void stopRequested();
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool event(QEvent *event) override;

private slots:
    void onFlickerTimer();

private:
    void drawOverlay(QPainter &painter);
    void drawBorder(QPainter &painter);
    void drawResizeHandles(QPainter &painter);
    void drawDimensions(QPainter &painter);
    void drawInstructions(QPainter &painter);
    void drawMatchFailedMessage(QPainter &painter);
    void drawScrollDirectionHint(QPainter &painter);
    void startMatchFailedAnimation();
    void stopMatchFailedAnimation();

    SelectionStateManager *m_selectionManager;
    BorderState m_borderState = BorderState::Selecting;
    CaptureDirection m_captureDirection = CaptureDirection::Vertical;
    bool m_regionLocked = false;
    bool m_allowMoveWhileCapturing = true;
    QScreen *m_screen = nullptr;
    bool m_allowNewSelection = true;
    QString m_matchFailedMessage;

    // Constrained move during capture
    bool m_isConstrainedMoving = false;
    QPoint m_constrainedMoveStart;
    QRect m_constrainedMoveOriginalRect;

    // Match failed animation
    QTimer *m_flickerTimer;
    bool m_flickerState = false;
    int m_flickerCount = 0;

    // Border colors
    static const QColor BORDER_SELECTING;
    static const QColor BORDER_ADJUSTING;
    static const QColor BORDER_CAPTURING;
    static const QColor BORDER_MATCH_FAIL;

    static constexpr int BORDER_WIDTH = 3;
    static constexpr int HANDLE_SIZE = 8;
    static constexpr int FLICKER_INTERVAL_MS = 150;
    static constexpr int FLICKER_MAX_COUNT = 6;
};

#endif // SCROLLINGCAPTUREOVERLAY_H
