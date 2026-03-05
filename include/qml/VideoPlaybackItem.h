#pragma once

#include <QQuickPaintedItem>
#include <QImage>
#include <QSize>

class IVideoPlayer;

/**
 * @brief QQuickPaintedItem that wraps IVideoPlayer for QML video playback.
 *
 * QML equivalent of VideoPlaybackWidget. Creates a platform-specific
 * IVideoPlayer via the factory method and renders decoded frames.
 *
 * Usage in QML:
 *   VideoPlaybackItem {
 *       source: "/path/to/video.mp4"
 *       onVideoLoaded: play()
 *   }
 */
class VideoPlaybackItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY stateChanged)
    Q_PROPERTY(int playerState READ playerState NOTIFY stateChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(qreal playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(qreal frameRate READ frameRate NOTIFY videoLoaded)
    Q_PROPERTY(int frameIntervalMs READ frameIntervalMs NOTIFY videoLoaded)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoLoaded)

public:
    explicit VideoPlaybackItem(QQuickItem *parent = nullptr);
    ~VideoPlaybackItem() override;

    void paint(QPainter *painter) override;

    // Properties
    QString source() const { return m_source; }
    void setSource(const QString &source);

    bool isPlaying() const;
    int playerState() const;
    qint64 position() const;
    qint64 duration() const;

    bool isMuted() const;
    void setMuted(bool muted);

    qreal volume() const;
    void setVolume(qreal volume);

    qreal playbackRate() const;
    void setPlaybackRate(qreal rate);

    qreal frameRate() const;
    int frameIntervalMs() const;
    QSize videoSize() const;

    // Invokable from QML
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void setLooping(bool loop);

signals:
    void sourceChanged();
    void stateChanged();
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void mutedChanged();
    void volumeChanged();
    void playbackRateChanged();
    void videoLoaded();
    void errorOccurred(const QString &message);
    void playbackFinished();
    void frameReady(const QImage &frame);

private slots:
    void onFrameReady(const QImage &frame);
    void onMediaLoaded();

private:
    void createPlayer();
    void refreshScaledFrameForCurrentSize();

    IVideoPlayer *m_player = nullptr;
    QString m_source;
    QImage m_currentFrame;
    QImage m_scaledFrame;
    QSize m_lastItemSize;
    QSize m_lastFrameSize;
    QSize m_targetScaledSize;
};
