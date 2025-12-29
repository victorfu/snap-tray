#ifndef RECORDINGBOUNDARYOVERLAY_H
#define RECORDINGBOUNDARYOVERLAY_H

#include <QWidget>
#include <QRect>
#include <QTimer>

class RecordingBoundaryOverlay : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Border animation mode
     */
    enum class BorderMode {
        Recording,  // Animated rotating gradient (blue-indigo-purple)
        Playing,    // Pulsing green animation
        Paused      // Static amber border
    };

    explicit RecordingBoundaryOverlay(QWidget *parent = nullptr);
    ~RecordingBoundaryOverlay();

    void setRegion(const QRect &region);
    void setBorderMode(BorderMode mode);
    BorderMode borderMode() const { return m_borderMode; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawRecordingBorder(QPainter &painter, const QRect &borderRect);
    void drawPlayingBorder(QPainter &painter, const QRect &borderRect);
    void drawPausedBorder(QPainter &painter, const QRect &borderRect);

    QRect m_region;
    QTimer *m_animationTimer;
    qreal m_gradientOffset;
    BorderMode m_borderMode;

    static const int BORDER_WIDTH = 4;
    static const int CORNER_RADIUS = 10;
};

#endif // RECORDINGBOUNDARYOVERLAY_H
