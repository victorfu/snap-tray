#ifndef RECORDINGCONTROLBAR_H
#define RECORDINGCONTROLBAR_H

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QShortcut>

class QLabel;
class QPushButton;

class RecordingControlBar : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingControlBar(QWidget *parent = nullptr);
    ~RecordingControlBar();

    void positionNear(const QRect &recordingRegion);
    void updateDuration(qint64 elapsedMs);
    void setPaused(bool paused);
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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupUi();
    QString formatDuration(qint64 ms) const;
    void updatePauseButton();
    void updateIndicatorGradient();

    QLabel *m_recordingIndicator;
    QLabel *m_audioIndicator;
    QLabel *m_durationLabel;
    QLabel *m_sizeLabel;
    QLabel *m_fpsLabel;
    QPushButton *m_pauseButton;
    QPushButton *m_stopButton;
    QPushButton *m_cancelButton;
    bool m_audioEnabled;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging;

    // Animation timer for indicator gradient
    QTimer *m_blinkTimer;
    qreal m_gradientOffset;
    bool m_indicatorVisible;
    bool m_isPaused;

    // Global shortcut for ESC key
    QShortcut *m_escShortcut;
};

#endif // RECORDINGCONTROLBAR_H
