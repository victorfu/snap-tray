#ifndef IVIDEOPLAYER_H
#define IVIDEOPLAYER_H

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

class IVideoPlayer : public QObject
{
    Q_OBJECT

public:
    enum class State { Stopped, Playing, Paused };
    Q_ENUM(State)

    explicit IVideoPlayer(QObject *parent = nullptr) : QObject(parent) {}
    ~IVideoPlayer() override = default;

    // Lifecycle
    virtual bool load(const QString &filePath) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 positionMs) = 0;

    // State queries
    virtual State state() const = 0;
    virtual qint64 duration() const = 0;      // Total duration in ms
    virtual qint64 position() const = 0;      // Current position in ms
    virtual QSize videoSize() const = 0;      // Native video dimensions
    virtual bool hasVideo() const = 0;

    // Volume control
    virtual void setVolume(float volume) = 0; // 0.0 - 1.0
    virtual float volume() const = 0;
    virtual void setMuted(bool muted) = 0;
    virtual bool isMuted() const = 0;

    // Looping control
    virtual void setLooping(bool loop) = 0;
    virtual bool isLooping() const = 0;

    // Playback rate control
    virtual void setPlaybackRate(float rate) = 0;  // 0.25 - 2.0
    virtual float playbackRate() const = 0;

    // Factory method
    static IVideoPlayer* create(QObject *parent = nullptr);
    static bool isAvailable();

signals:
    void stateChanged(IVideoPlayer::State state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void frameReady(const QImage &frame);
    void error(const QString &message);
    void mediaLoaded();
    void playbackFinished();
    void playbackRateChanged(float rate);
};

#endif // IVIDEOPLAYER_H
