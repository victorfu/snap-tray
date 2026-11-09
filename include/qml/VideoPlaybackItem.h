#pragma once

#include <QQuickPaintedItem>
#include <functional>
#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QRectF>
#include <QColor>

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
    // The time of the frame on screen, updated with every frame: what
    // overlays drawn over the video follow (position may be reported only
    // every 100 ms while playing). A paused seek presents the source frame
    // at the requested time, which can be later than its PTS for sparse video.
    Q_PROPERTY(qint64 framePosition READ framePosition NOTIFY framePositionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool audioPlaybackSupported READ audioPlaybackSupported NOTIFY audioPlaybackSupportedChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(qreal playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(qreal frameRate READ frameRate NOTIFY videoLoaded)
    Q_PROPERTY(int frameIntervalMs READ frameIntervalMs NOTIFY videoLoaded)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoLoaded)
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentRectChanged)
    // Fills the area around the picture (letterbox); the editor uses its canvas colour.
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)

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
    qint64 framePosition() const { return m_framePosition; }
    qint64 duration() const;
    bool audioPlaybackSupported() const;

    bool isMuted() const;
    void setMuted(bool muted);

    qreal volume() const;
    void setVolume(qreal volume);

    qreal playbackRate() const;
    void setPlaybackRate(qreal rate);

    qreal frameRate() const;
    int frameIntervalMs() const;
    QSize videoSize() const;
    QRectF contentRect() const { return m_contentRect; }
    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor& color);

    // Invokable from QML
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void stepForward();
    Q_INVOKABLE void setLooping(bool loop);

    // Processes each shown frame (before scaling) with its own time (framePosition),
    // e.g. to burn in the export's masks so the preview never shows what the
    // export hides. The raw frame is kept; reprocessFrame() redoes the shown one.
    using FrameProcessor = std::function<void(QImage& frame, qint64 positionMs)>;
    void setFrameProcessor(FrameProcessor processor);
    // #189: audio to play instead of the file's track; false where unsupported.
    bool setAudioSource(std::function<QByteArray(qint64 startFrame, int frames)> source);
    void reprocessFrame();

signals:
    void audioPlaybackSupportedChanged();
    void sourceChanged();
    void stateChanged();
    void positionChanged(qint64 positionMs);
    void framePositionChanged();
    void durationChanged(qint64 durationMs);
    void mutedChanged();
    void volumeChanged();
    void playbackRateChanged();
    void videoLoaded();
    void contentRectChanged();
    void backgroundColorChanged();
    void errorOccurred(const QString &message);
    void playbackFinished();
    void frameReady(const QImage &frame);

private slots:
    void onFrameReady(const QImage &frame, qint64 positionMs = -1);
    void onMediaLoaded();

private:
    void createPlayer();
    void setFramePosition(qint64 positionMs);
    void refreshScaledFrameForCurrentSize();
    // Drops the shown frame and empties contentRect (emitting contentRectChanged).
    void clearFrame();

    IVideoPlayer *m_player = nullptr;
    QString m_source;
    QColor m_backgroundColor = Qt::black;
    QImage m_currentFrame;
    QImage m_rawFrame;
    FrameProcessor m_frameProcessor;
    qint64 m_framePosition = 0;
    qint64 m_pausedSeekPosition = -1;
    QImage m_scaledFrame;
    QSize m_lastItemSize;
    QSize m_lastFrameSize;
    QSize m_targetScaledSize;
    QRectF m_contentRect;
};
