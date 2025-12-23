#ifndef RECORDINGCONTROLBAR_H
#define RECORDINGCONTROLBAR_H

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QTimer>

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

    QLabel *m_recordingIndicator;
    QLabel *m_durationLabel;
    QPushButton *m_pauseButton;
    QPushButton *m_stopButton;
    QPushButton *m_cancelButton;

    // Drag support
    QPoint m_dragStartPos;
    QPoint m_dragStartWidgetPos;
    bool m_isDragging;

    // Blink timer for recording indicator
    QTimer *m_blinkTimer;
    bool m_indicatorVisible;
    bool m_isPaused;
};

#endif // RECORDINGCONTROLBAR_H
