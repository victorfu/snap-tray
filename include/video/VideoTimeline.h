#ifndef VIDEOTIMELINE_H
#define VIDEOTIMELINE_H

#include <QWidget>

class VideoTimeline : public QWidget
{
    Q_OBJECT

public:
    explicit VideoTimeline(QWidget *parent = nullptr);

    void setDuration(qint64 ms);
    void setPosition(qint64 ms);

    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }
    bool isScrubbing() const { return m_scrubbing; }

signals:
    void seekRequested(qint64 positionMs);
    void scrubbingStarted();
    void scrubbingEnded();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    // Helper methods accessible by subclasses
    qint64 positionFromX(int x) const;
    int xFromPosition(qint64 pos) const;
    QString formatTime(qint64 ms) const;
    QRect trackRect() const;
    QRect playheadRect() const;

    qint64 m_duration = 0;
    qint64 m_position = 0;
    bool m_scrubbing = false;
    bool m_hovering = false;
    int m_hoverX = 0;

    // Layout constants
    static constexpr int kHeight = 24;
    static constexpr int kTrackHeight = 4;
    static constexpr int kPlayheadSize = 12;
    static constexpr int kHorizontalPadding = 8;
};

#endif // VIDEOTIMELINE_H
