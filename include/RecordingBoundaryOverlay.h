#ifndef RECORDINGBOUNDARYOVERLAY_H
#define RECORDINGBOUNDARYOVERLAY_H

#include <QWidget>
#include <QRect>
#include <QTimer>

class RecordingBoundaryOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingBoundaryOverlay(QWidget *parent = nullptr);
    ~RecordingBoundaryOverlay();

    void setRegion(const QRect &region);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRect m_region;
    QTimer *m_animationTimer;
    qreal m_gradientOffset;

    static const int BORDER_WIDTH = 4;
    static const int CORNER_RADIUS = 10;
};

#endif // RECORDINGBOUNDARYOVERLAY_H
