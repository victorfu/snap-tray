#include "video/VideoTimeline.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QWheelEvent>

VideoTimeline::VideoTimeline(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kHeight);
}

void VideoTimeline::setDuration(qint64 ms)
{
    if (m_duration != ms) {
        m_duration = ms;
        update();
    }
}

void VideoTimeline::setPosition(qint64 ms)
{
    if (!m_scrubbing && m_position != ms) {
        m_position = qBound(qint64(0), ms, m_duration);
        update();
    }
}

QSize VideoTimeline::sizeHint() const
{
    return QSize(200, kHeight);
}

QSize VideoTimeline::minimumSizeHint() const
{
    return QSize(100, kHeight);
}

QRect VideoTimeline::trackRect() const
{
    int y = (height() - kTrackHeight) / 2;
    return QRect(kHorizontalPadding, y,
                 width() - 2 * kHorizontalPadding, kTrackHeight);
}

QRect VideoTimeline::playheadRect() const
{
    int x = xFromPosition(m_position);
    int y = (height() - kPlayheadSize) / 2;
    return QRect(x - kPlayheadSize / 2, y, kPlayheadSize, kPlayheadSize);
}

qint64 VideoTimeline::positionFromX(int x) const
{
    if (m_duration <= 0) return 0;

    QRect track = trackRect();
    if (track.width() <= 0) return 0;  // Prevent division by zero

    int clampedX = qBound(track.left(), x, track.right());
    double ratio = double(clampedX - track.left()) / track.width();
    return qint64(ratio * m_duration);
}

int VideoTimeline::xFromPosition(qint64 pos) const
{
    if (m_duration <= 0) return kHorizontalPadding;

    QRect track = trackRect();
    double ratio = double(pos) / m_duration;
    return track.left() + int(ratio * track.width());
}

QString VideoTimeline::formatTime(qint64 ms) const
{
    qint64 totalSeconds = ms / 1000;
    int minutes = int(totalSeconds / 60);
    int seconds = int(totalSeconds % 60);
    int millis = int(ms % 1000);

    if (minutes > 0) {
        return QString("%1:%2.%3")
            .arg(minutes)
            .arg(seconds, 2, 10, QChar('0'))
            .arg(millis / 100);
    } else {
        return QString("%1.%2s")
            .arg(seconds)
            .arg(millis / 100);
    }
}

void VideoTimeline::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect track = trackRect();

    // Draw track background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(60, 60, 60));
    painter.drawRoundedRect(track, kTrackHeight / 2, kTrackHeight / 2);

    // Draw progress (filled portion)
    if (m_duration > 0 && m_position > 0) {
        int progressX = xFromPosition(m_position);
        QRect progressRect(track.left(), track.top(),
                           progressX - track.left(), track.height());
        painter.setBrush(QColor(0, 122, 255));  // macOS accent blue
        painter.drawRoundedRect(progressRect, kTrackHeight / 2, kTrackHeight / 2);
    }

    // Draw hover time indicator
    if (m_hovering && m_duration > 0 && !m_scrubbing) {
        int hoverX = qBound(track.left(), m_hoverX, track.right());
        painter.setPen(QPen(QColor(255, 255, 255, 100), 1));
        painter.drawLine(hoverX, track.top(), hoverX, track.bottom());
    }

    // Draw playhead
    if (m_duration > 0) {
        QRect head = playheadRect();
        int centerX = head.center().x();
        int centerY = head.center().y();
        int radius = kPlayheadSize / 2;

        // Shadow
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 80));
        painter.drawEllipse(QPoint(centerX + 1, centerY + 1), radius, radius);

        // Playhead
        painter.setBrush(Qt::white);
        painter.drawEllipse(QPoint(centerX, centerY), radius, radius);

        // Highlight when scrubbing or hovering near playhead
        if (m_scrubbing) {
            painter.setBrush(QColor(0, 122, 255));
            painter.drawEllipse(QPoint(centerX, centerY), radius - 2, radius - 2);
        }
    }
}

void VideoTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_duration > 0) {
        m_scrubbing = true;
        m_position = positionFromX(event->pos().x());
        emit scrubbingStarted();
        emit seekRequested(m_position);
        update();
    }
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *event)
{
    m_hoverX = event->pos().x();

    if (m_scrubbing) {
        m_position = positionFromX(event->pos().x());
        emit seekRequested(m_position);
        update();
    } else if (m_hovering && m_duration > 0) {
        // Show time tooltip on hover
        qint64 hoverPos = positionFromX(m_hoverX);
        QToolTip::showText(event->globalPosition().toPoint(),
                           formatTime(hoverPos), this);
        update();
    }
}

void VideoTimeline::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_scrubbing) {
        m_scrubbing = false;
        emit scrubbingEnded();
        update();
    }
}

void VideoTimeline::wheelEvent(QWheelEvent *event)
{
    if (m_duration <= 0) return;

    // Scroll ±1 second
    int delta = event->angleDelta().y() > 0 ? 1000 : -1000;
    qint64 newPos = qBound(qint64(0), m_position + delta, m_duration);

    if (newPos != m_position) {
        m_position = newPos;
        emit seekRequested(m_position);
        update();
    }
}

void VideoTimeline::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event)
    m_hovering = true;
    update();
}

void VideoTimeline::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    m_hovering = false;
    QToolTip::hideText();
    update();
}
