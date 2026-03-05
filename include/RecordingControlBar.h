#ifndef RECORDINGCONTROLBAR_H
#define RECORDINGCONTROLBAR_H

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QShortcut>
#include <QColor>

class QLabel;
class QHBoxLayout;
class QEnterEvent;
namespace SnapTray { class GlassTooltip; }
class RecordingControlBar : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingControlBar(QWidget *parent = nullptr);
    ~RecordingControlBar();

    void positionNear(const QRect &recordingRegion);
    void updateDuration(qint64 elapsedMs);
    void setPaused(bool paused);
    void setPreparing(bool preparing);
    void setPreparingStatus(const QString &status);
    void updateRegionSize(int width, int height);
    void updateFps(double fps);
    void setAudioEnabled(bool enabled);

signals:
    void stopRequested();
    void cancelRequested();
    void pauseRequested();
    void resumeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum ButtonId {
        ButtonNone = -1,
        ButtonPause = 0,
        ButtonStop = 1,
        ButtonCancel = 2,
    };

    void setupUi();
    QString formatDuration(qint64 ms) const;
    void updateIndicatorGradient();
    void updateButtonRects();
    int buttonAtPosition(const QPoint &pos) const;
    void drawButtons(QPainter &painter);
    void updateButtonSpacerWidth();
    QString tooltipForButton(int button) const;
    QRect backgroundRect() const;
    QRect anchorRectForButton(int button) const;
    void updateFixedWidth();
    void applyFixedWidth(int targetWidth);
    void showTooltipForButton(int buttonId);
    void showTooltip(const QString &text, const QRect &anchorRect);
    void hideTooltip();

    // Info labels
    QLabel *m_recordingIndicator;
    QLabel *m_audioIndicator;
    QLabel *m_durationLabel;
    QLabel *m_sizeLabel;
    QLabel *m_fpsLabel;
    QLabel *m_separator1;
    QLabel *m_separator2;
    QWidget *m_buttonSpacer;
    QHBoxLayout *m_layout;
    bool m_audioEnabled;

    // Button rectangles
    QRect m_pauseRect;
    QRect m_stopRect;
    QRect m_cancelRect;
    int m_hoveredButton;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging;

    // Animation timer for indicator gradient
    QTimer *m_blinkTimer;
    qreal m_gradientOffset;
    bool m_isPaused;
    bool m_isPreparing;

    // Global shortcut for ESC key
    QShortcut *m_escShortcut;
    SnapTray::GlassTooltip *m_tooltip;

    // Layout constants (matching ToolbarCore)
    static const int TOOLBAR_HEIGHT = 32;
    static const int BUTTON_WIDTH = 28;
    static const int BUTTON_SPACING = 2;
    static const int SEPARATOR_MARGIN = 8;
    static const int SHADOW_MARGIN = 0;
    static const int SHADOW_MARGIN_X = 0;
};

#endif // RECORDINGCONTROLBAR_H
