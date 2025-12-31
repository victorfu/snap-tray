#include "video/VideoPlaybackWidget.h"

#include <QDebug>
#include <QMouseEvent>
#include <QPainter>

VideoPlaybackWidget::VideoPlaybackWidget(QWidget *parent)
    : QWidget(parent)
    , m_player(nullptr)
    , m_videoLoaded(false)
{
    // No minimum size - allow the widget to match the recording region exactly
    // The parent (InPlacePreviewOverlay) sets the geometry based on recording region

    // Black background
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    // Create video player
    m_player = IVideoPlayer::create(this);
    if (!m_player) {
        qWarning() << "VideoPlaybackWidget: Failed to create video player";
        return;
    }

    // Connect signals
    connect(m_player, &IVideoPlayer::frameReady,
            this, &VideoPlaybackWidget::onFrameReady);
    connect(m_player, &IVideoPlayer::stateChanged,
            this, &VideoPlaybackWidget::stateChanged);
    connect(m_player, &IVideoPlayer::positionChanged,
            this, &VideoPlaybackWidget::positionChanged);
    connect(m_player, &IVideoPlayer::durationChanged,
            this, &VideoPlaybackWidget::durationChanged);
    connect(m_player, &IVideoPlayer::error,
            this, &VideoPlaybackWidget::errorOccurred);
    connect(m_player, &IVideoPlayer::mediaLoaded, this, [this]() {
        m_videoLoaded = true;
        emit videoLoaded();
    });
    connect(m_player, &IVideoPlayer::playbackFinished,
            this, &VideoPlaybackWidget::playbackFinished);
    connect(m_player, &IVideoPlayer::playbackRateChanged,
            this, &VideoPlaybackWidget::playbackRateChanged);
}

VideoPlaybackWidget::~VideoPlaybackWidget()
{
    // m_player is parented to this, will be deleted automatically
}

bool VideoPlaybackWidget::loadVideo(const QString &filePath)
{
    if (!m_player) {
        emit errorOccurred("Video player not available");
        return false;
    }

    m_videoLoaded = false;
    m_currentFrame = QImage();
    m_scaledFrame = QImage();
    update();

    return m_player->load(filePath);
}

void VideoPlaybackWidget::play()
{
    if (m_player) {
        m_player->play();
    }
}

void VideoPlaybackWidget::pause()
{
    if (m_player) {
        m_player->pause();
    }
}

void VideoPlaybackWidget::togglePlayPause()
{
    if (!m_player) return;

    if (m_player->state() == IVideoPlayer::State::Playing) {
        pause();
    } else {
        play();
    }
}

void VideoPlaybackWidget::stop()
{
    if (m_player) {
        m_player->stop();
        m_currentFrame = QImage();
        m_scaledFrame = QImage();
        update();
    }
}

void VideoPlaybackWidget::seek(qint64 positionMs)
{
    if (m_player) {
        m_player->seek(positionMs);
    }
}

IVideoPlayer::State VideoPlaybackWidget::state() const
{
    return m_player ? m_player->state() : IVideoPlayer::State::Stopped;
}

qint64 VideoPlaybackWidget::duration() const
{
    return m_player ? m_player->duration() : 0;
}

qint64 VideoPlaybackWidget::position() const
{
    return m_player ? m_player->position() : 0;
}

bool VideoPlaybackWidget::isVideoLoaded() const
{
    return m_videoLoaded;
}

void VideoPlaybackWidget::setVolume(float volume)
{
    if (m_player) {
        m_player->setVolume(volume);
    }
}

float VideoPlaybackWidget::volume() const
{
    return m_player ? m_player->volume() : 1.0f;
}

void VideoPlaybackWidget::setMuted(bool muted)
{
    if (m_player) {
        m_player->setMuted(muted);
    }
}

bool VideoPlaybackWidget::isMuted() const
{
    return m_player ? m_player->isMuted() : false;
}

void VideoPlaybackWidget::setLooping(bool loop)
{
    if (m_player) {
        m_player->setLooping(loop);
    }
}

bool VideoPlaybackWidget::isLooping() const
{
    return m_player ? m_player->isLooping() : false;
}

void VideoPlaybackWidget::setPlaybackRate(float rate)
{
    if (m_player) {
        m_player->setPlaybackRate(rate);
    }
}

float VideoPlaybackWidget::playbackRate() const
{
    return m_player ? m_player->playbackRate() : 1.0f;
}

QSize VideoPlaybackWidget::videoSize() const
{
    return m_player ? m_player->videoSize() : QSize();
}

void VideoPlaybackWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Fill background with black
    painter.fillRect(rect(), Qt::black);

    if (m_scaledFrame.isNull()) {
        // Show placeholder text
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, "No video loaded");
        return;
    }

    // Center the scaled frame (letterboxing)
    int x = (width() - m_scaledFrame.width()) / 2;
    int y = (height() - m_scaledFrame.height()) / 2;
    painter.drawImage(x, y, m_scaledFrame);
}

void VideoPlaybackWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // Recalculate scaled frame if we have a current frame
    if (!m_currentFrame.isNull() && size() != m_lastWidgetSize) {
        m_lastWidgetSize = size();

        // Scale frame to fit widget while maintaining aspect ratio
        m_scaledFrame = m_currentFrame.scaled(size(), Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
    }
}

void VideoPlaybackWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    togglePlayPause();
}

void VideoPlaybackWidget::onFrameReady(const QImage &frame)
{
    if (frame.isNull()) return;

    m_currentFrame = frame;

    // Scale frame to fit widget
    if (size() != m_lastWidgetSize || m_scaledFrame.isNull()) {
        m_lastWidgetSize = size();
        m_scaledFrame = frame.scaled(size(), Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
    } else {
        // Fast path: same size, just scale
        m_scaledFrame = frame.scaled(size(), Qt::KeepAspectRatio,
                                     Qt::FastTransformation);
    }

    update();

    // Re-emit for external consumers (e.g., format conversion)
    emit frameReady(frame);
}
