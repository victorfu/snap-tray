#ifndef INPLACEPREVIEWOVERLAY_H
#define INPLACEPREVIEWOVERLAY_H

#include "video/IVideoPlayer.h"
#include <QWidget>
#include <QRect>

class VideoPlaybackWidget;
class QScreen;

/**
 * @brief A transparent overlay window that hosts video playback
 * positioned exactly over the recording region.
 */
class InPlacePreviewOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit InPlacePreviewOverlay(QWidget *parent = nullptr);
    ~InPlacePreviewOverlay() override;

    /**
     * @brief Set the region and screen where the preview should appear.
     * @param region The recording region in screen coordinates.
     * @param screen The screen where the region is located.
     */
    void setRegion(const QRect &region, QScreen *screen);

    /**
     * @brief Load a video file for playback.
     * @param videoPath Path to the video file.
     * @return true if loading was successful.
     */
    bool loadVideo(const QString &videoPath);

    // Playback control passthrough
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(qint64 positionMs);

    // Volume control
    void setVolume(float volume);
    float volume() const;
    void setMuted(bool muted);
    bool isMuted() const;

    // Playback rate
    void setPlaybackRate(float rate);
    float playbackRate() const;

    // Looping
    void setLooping(bool loop);
    bool isLooping() const;

    // State queries
    IVideoPlayer::State state() const;
    qint64 duration() const;
    qint64 position() const;
    QSize videoSize() const;
    bool isVideoLoaded() const;

signals:
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void stateChanged(IVideoPlayer::State state);
    void videoLoaded();
    void errorOccurred(const QString &message);
    void frameReady(const QImage &frame);
    void playbackFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    VideoPlaybackWidget *m_videoWidget;
    QRect m_region;
    QScreen *m_targetScreen;
};

#endif // INPLACEPREVIEWOVERLAY_H
