#include "qml/VideoPlaybackItem.h"
#include "video/IVideoPlayer.h"

#include <QPainter>
#include <QDebug>

VideoPlaybackItem::VideoPlaybackItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAntialiasing(true);
    createPlayer();

    connect(this, &QQuickItem::widthChanged, this, [this]() {
        refreshScaledFrameForCurrentSize();
    });
    connect(this, &QQuickItem::heightChanged, this, [this]() {
        refreshScaledFrameForCurrentSize();
    });
}

VideoPlaybackItem::~VideoPlaybackItem()
{
    // m_player is parented to this via QObject, deleted automatically
}

void VideoPlaybackItem::createPlayer()
{
    m_player = IVideoPlayer::create(this);
    if (!m_player) {
        qWarning() << "VideoPlaybackItem: Failed to create video player";
        return;
    }

    connect(m_player, &IVideoPlayer::frameReady,
            this, &VideoPlaybackItem::onFrameReady, Qt::QueuedConnection);
    connect(m_player, &IVideoPlayer::stateChanged,
            this, &VideoPlaybackItem::stateChanged);
    connect(m_player, &IVideoPlayer::positionChanged,
            this, &VideoPlaybackItem::positionChanged);
    connect(m_player, &IVideoPlayer::durationChanged,
            this, &VideoPlaybackItem::durationChanged);
    connect(m_player, &IVideoPlayer::error,
            this, &VideoPlaybackItem::errorOccurred);
    connect(m_player, &IVideoPlayer::mediaLoaded,
            this, &VideoPlaybackItem::onMediaLoaded);
    connect(m_player, &IVideoPlayer::playbackFinished,
            this, &VideoPlaybackItem::playbackFinished);
    connect(m_player, &IVideoPlayer::playbackRateChanged,
            this, [this]() { emit playbackRateChanged(); });
}

void VideoPlaybackItem::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->fillRect(contentsBoundingRect(), Qt::black);

    if (m_scaledFrame.isNull()) {
        painter->setPen(Qt::gray);
        painter->drawText(contentsBoundingRect(), Qt::AlignCenter, "No video loaded");
        return;
    }

    // Center the scaled frame (letterboxing)
    int x = (width() - m_scaledFrame.width()) / 2;
    int y = (height() - m_scaledFrame.height()) / 2;
    painter->drawImage(x, y, m_scaledFrame);
}

void VideoPlaybackItem::setSource(const QString &source)
{
    if (m_source == source)
        return;

    m_source = source;
    emit sourceChanged();

    if (!m_player || source.isEmpty())
        return;

    m_currentFrame = QImage();
    m_scaledFrame = QImage();
    update();

    if (!m_player->load(source)) {
        const QString message = QStringLiteral("Failed to load: %1").arg(source);
        qWarning() << "VideoPlaybackItem:" << message;
        emit errorOccurred(message);
    }
}

bool VideoPlaybackItem::isPlaying() const
{
    return m_player && m_player->state() == IVideoPlayer::State::Playing;
}

int VideoPlaybackItem::playerState() const
{
    return m_player ? static_cast<int>(m_player->state()) : 0;
}

qint64 VideoPlaybackItem::position() const
{
    return m_player ? m_player->position() : 0;
}

qint64 VideoPlaybackItem::duration() const
{
    return m_player ? m_player->duration() : 0;
}

bool VideoPlaybackItem::isMuted() const
{
    return m_player ? m_player->isMuted() : false;
}

void VideoPlaybackItem::setMuted(bool muted)
{
    if (!m_player || m_player->isMuted() == muted)
        return;
    m_player->setMuted(muted);
    emit mutedChanged();
}

qreal VideoPlaybackItem::volume() const
{
    return m_player ? m_player->volume() : 1.0;
}

void VideoPlaybackItem::setVolume(qreal vol)
{
    if (!m_player || qFuzzyCompare(m_player->volume(), static_cast<float>(vol)))
        return;
    m_player->setVolume(static_cast<float>(vol));
    emit volumeChanged();
}

qreal VideoPlaybackItem::playbackRate() const
{
    return m_player ? m_player->playbackRate() : 1.0;
}

void VideoPlaybackItem::setPlaybackRate(qreal rate)
{
    if (!m_player)
        return;
    m_player->setPlaybackRate(static_cast<float>(rate));
}

qreal VideoPlaybackItem::frameRate() const
{
    return m_player ? m_player->frameRate() : 30.0;
}

int VideoPlaybackItem::frameIntervalMs() const
{
    return m_player ? m_player->frameIntervalMs() : 33;
}

QSize VideoPlaybackItem::videoSize() const
{
    return m_player ? m_player->videoSize() : QSize();
}

void VideoPlaybackItem::play()
{
    if (m_player)
        m_player->play();
}

void VideoPlaybackItem::pause()
{
    if (m_player)
        m_player->pause();
}

void VideoPlaybackItem::togglePlayPause()
{
    if (!m_player)
        return;
    if (m_player->state() == IVideoPlayer::State::Playing)
        pause();
    else
        play();
}

void VideoPlaybackItem::stop()
{
    if (m_player) {
        m_player->stop();
        m_currentFrame = QImage();
        m_scaledFrame = QImage();
        update();
    }
}

void VideoPlaybackItem::seek(qint64 positionMs)
{
    if (m_player) {
        positionMs = qBound(qint64(0), positionMs, duration());
        m_player->seek(positionMs);
    }
}

void VideoPlaybackItem::setLooping(bool loop)
{
    if (m_player)
        m_player->setLooping(loop);
}

void VideoPlaybackItem::onFrameReady(const QImage &frame)
{
    if (frame.isNull())
        return;

    m_currentFrame = frame;
    refreshScaledFrameForCurrentSize();

    // Re-emit for external consumers (format conversion)
    emit frameReady(frame);
}

void VideoPlaybackItem::onMediaLoaded()
{
    qDebug() << "VideoPlaybackItem: Media loaded, duration:" << duration();
    emit videoLoaded();
}

void VideoPlaybackItem::refreshScaledFrameForCurrentSize()
{
    if (m_currentFrame.isNull())
        return;

    QSize itemSize(static_cast<int>(width()), static_cast<int>(height()));
    if (itemSize.isEmpty())
        return;

    // Only recalculate target size when dimensions change
    if (itemSize != m_lastItemSize || m_currentFrame.size() != m_lastFrameSize) {
        m_lastItemSize = itemSize;
        m_lastFrameSize = m_currentFrame.size();
        m_targetScaledSize = m_currentFrame.size().scaled(itemSize, Qt::KeepAspectRatio);
    }

    // Scale only if needed
    if (m_currentFrame.size() == m_targetScaledSize) {
        m_scaledFrame = m_currentFrame;
    } else {
        m_scaledFrame = m_currentFrame.scaled(m_targetScaledSize, Qt::IgnoreAspectRatio,
                                              Qt::SmoothTransformation);
    }

    update();
}
