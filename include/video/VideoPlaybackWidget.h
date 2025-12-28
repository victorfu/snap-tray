#ifndef VIDEOPLAYBACKWIDGET_H
#define VIDEOPLAYBACKWIDGET_H

#include "video/IVideoPlayer.h"
#include <QWidget>

class VideoPlaybackWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPlaybackWidget(QWidget *parent = nullptr);
    ~VideoPlaybackWidget() override;

    bool loadVideo(const QString &filePath);
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(qint64 positionMs);

    // State queries
    IVideoPlayer::State state() const;
    qint64 duration() const;
    qint64 position() const;
    bool isVideoLoaded() const;

    // Volume control
    void setVolume(float volume);
    float volume() const;
    void setMuted(bool muted);
    bool isMuted() const;

    // Looping
    void setLooping(bool loop);
    bool isLooping() const;

    // Playback rate
    void setPlaybackRate(float rate);
    float playbackRate() const;

    // Video properties
    QSize videoSize() const;

signals:
    void frameReady(const QImage &frame);
    void stateChanged(IVideoPlayer::State state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void errorOccurred(const QString &message);
    void videoLoaded();
    void playbackFinished();
    void playbackRateChanged(float rate);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    void onFrameReady(const QImage &frame);

private:
    IVideoPlayer *m_player;
    QImage m_currentFrame;
    QImage m_scaledFrame;
    QSize m_lastWidgetSize;
    bool m_videoLoaded;
};

#endif // VIDEOPLAYBACKWIDGET_H
