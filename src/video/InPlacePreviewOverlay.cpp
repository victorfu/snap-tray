#include "video/InPlacePreviewOverlay.h"
#include "video/VideoPlaybackWidget.h"

#include <QScreen>
#include <QPainter>
#include <QKeyEvent>
#include <QVBoxLayout>

InPlacePreviewOverlay::InPlacePreviewOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint)
    , m_videoWidget(nullptr)
    , m_targetScreen(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("border: none;");

    // Create video widget that fills the overlay
    m_videoWidget = new VideoPlaybackWidget(this);

    // Use a layout to make the video widget fill the overlay
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_videoWidget);

    // Forward signals from video widget
    connect(m_videoWidget, &VideoPlaybackWidget::positionChanged,
            this, &InPlacePreviewOverlay::positionChanged);
    connect(m_videoWidget, &VideoPlaybackWidget::durationChanged,
            this, &InPlacePreviewOverlay::durationChanged);
    connect(m_videoWidget, &VideoPlaybackWidget::stateChanged,
            this, &InPlacePreviewOverlay::stateChanged);
    connect(m_videoWidget, &VideoPlaybackWidget::videoLoaded,
            this, &InPlacePreviewOverlay::videoLoaded);
    connect(m_videoWidget, &VideoPlaybackWidget::errorOccurred,
            this, &InPlacePreviewOverlay::errorOccurred);
    connect(m_videoWidget, &VideoPlaybackWidget::frameReady,
            this, &InPlacePreviewOverlay::frameReady);
    connect(m_videoWidget, &VideoPlaybackWidget::playbackFinished,
            this, &InPlacePreviewOverlay::playbackFinished);
}

InPlacePreviewOverlay::~InPlacePreviewOverlay()
{
}

void InPlacePreviewOverlay::setRegion(const QRect &region, QScreen *screen)
{
    m_region = region;
    m_targetScreen = screen;

    // Position the overlay exactly over the recording region
    setGeometry(region);
}

bool InPlacePreviewOverlay::loadVideo(const QString &videoPath)
{
    if (!m_videoWidget) {
        return false;
    }
    return m_videoWidget->loadVideo(videoPath);
}

void InPlacePreviewOverlay::play()
{
    if (m_videoWidget) {
        m_videoWidget->play();
    }
}

void InPlacePreviewOverlay::pause()
{
    if (m_videoWidget) {
        m_videoWidget->pause();
    }
}

void InPlacePreviewOverlay::togglePlayPause()
{
    if (m_videoWidget) {
        m_videoWidget->togglePlayPause();
    }
}

void InPlacePreviewOverlay::stop()
{
    if (m_videoWidget) {
        m_videoWidget->stop();
    }
}

void InPlacePreviewOverlay::seek(qint64 positionMs)
{
    if (m_videoWidget) {
        m_videoWidget->seek(positionMs);
    }
}

void InPlacePreviewOverlay::setVolume(float volume)
{
    if (m_videoWidget) {
        m_videoWidget->setVolume(volume);
    }
}

float InPlacePreviewOverlay::volume() const
{
    return m_videoWidget ? m_videoWidget->volume() : 1.0f;
}

void InPlacePreviewOverlay::setMuted(bool muted)
{
    if (m_videoWidget) {
        m_videoWidget->setMuted(muted);
    }
}

bool InPlacePreviewOverlay::isMuted() const
{
    return m_videoWidget ? m_videoWidget->isMuted() : false;
}

void InPlacePreviewOverlay::setPlaybackRate(float rate)
{
    if (m_videoWidget) {
        m_videoWidget->setPlaybackRate(rate);
    }
}

float InPlacePreviewOverlay::playbackRate() const
{
    return m_videoWidget ? m_videoWidget->playbackRate() : 1.0f;
}

void InPlacePreviewOverlay::setLooping(bool loop)
{
    if (m_videoWidget) {
        m_videoWidget->setLooping(loop);
    }
}

bool InPlacePreviewOverlay::isLooping() const
{
    return m_videoWidget ? m_videoWidget->isLooping() : false;
}

IVideoPlayer::State InPlacePreviewOverlay::state() const
{
    return m_videoWidget ? m_videoWidget->state() : IVideoPlayer::State::Stopped;
}

qint64 InPlacePreviewOverlay::duration() const
{
    return m_videoWidget ? m_videoWidget->duration() : 0;
}

qint64 InPlacePreviewOverlay::position() const
{
    return m_videoWidget ? m_videoWidget->position() : 0;
}

QSize InPlacePreviewOverlay::videoSize() const
{
    return m_videoWidget ? m_videoWidget->videoSize() : QSize();
}

bool InPlacePreviewOverlay::isVideoLoaded() const
{
    return m_videoWidget ? m_videoWidget->isVideoLoaded() : false;
}

void InPlacePreviewOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // The video widget handles its own painting
    // This overlay is transparent
}

void InPlacePreviewOverlay::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        togglePlayPause();
        break;
    case Qt::Key_Left:
        seek(qMax(0LL, position() - 5000));  // -5 seconds
        break;
    case Qt::Key_Right:
        seek(qMin(duration(), position() + 5000));  // +5 seconds
        break;
    case Qt::Key_M:
        setMuted(!isMuted());
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void InPlacePreviewOverlay::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    togglePlayPause();
}
