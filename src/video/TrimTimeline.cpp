#include "video/TrimTimeline.h"

#include <QMouseEvent>
#include <QPainter>
#include <QDebug>

TrimTimeline::TrimTimeline(QWidget *parent)
    : VideoTimeline(parent)
{
}

void TrimTimeline::setTrimRange(qint64 startMs, qint64 endMs)
{
    qint64 newStart = qMax(qint64(0), startMs);
    qint64 newEnd = endMs < 0 ? -1 : qMin(endMs, duration());

    if (m_trimStart != newStart || m_trimEnd != newEnd) {
        m_trimStart = newStart;
        m_trimEnd = newEnd;
        update();
        emit trimRangeChanged(m_trimStart, trimEnd());
    }
}

qint64 TrimTimeline::trimEnd() const
{
    return m_trimEnd < 0 ? duration() : m_trimEnd;
}

qint64 TrimTimeline::trimmedDuration() const
{
    return trimEnd() - m_trimStart;
}

bool TrimTimeline::hasTrim() const
{
    return m_trimStart > 0 || (m_trimEnd >= 0 && m_trimEnd < duration());
}

void TrimTimeline::resetTrim()
{
    if (m_trimStart != 0 || m_trimEnd != -1) {
        m_trimStart = 0;
        m_trimEnd = -1;
        update();
        emit trimRangeChanged(0, duration());
    }
}

QRect TrimTimeline::startHandleRect() const
{
    int x = xFromPosition(m_trimStart);
    int y = (height() - kHandleHeight) / 2;
    return QRect(x - kHandleWidth / 2, y, kHandleWidth, kHandleHeight);
}

QRect TrimTimeline::endHandleRect() const
{
    int x = xFromPosition(trimEnd());
    int y = (height() - kHandleHeight) / 2;
    return QRect(x - kHandleWidth / 2, y, kHandleWidth, kHandleHeight);
}

TrimTimeline::DragTarget TrimTimeline::hitTest(const QPoint &pos) const
{
    // Check handles first (higher priority)
    if (startHandleRect().contains(pos)) {
        return DragTarget::StartHandle;
    }
    if (endHandleRect().contains(pos)) {
        return DragTarget::EndHandle;
    }

    // Check playhead
    if (playheadRect().contains(pos)) {
        return DragTarget::Playhead;
    }

    return DragTarget::None;
}

void TrimTimeline::paintEvent(QPaintEvent *event)
{
    // Draw base timeline first
    VideoTimeline::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw trim overlay (excluded regions)
    drawTrimOverlay(painter);

    // Draw trim handles
    drawTrimHandles(painter);
}

void TrimTimeline::drawTrimOverlay(QPainter &painter)
{
    if (!hasTrim() || duration() <= 0) {
        return;
    }

    QRect track = trackRect();

    // Semi-transparent overlay for excluded regions
    QColor overlayColor(0, 0, 0, 128);  // 50% black

    // Left excluded region (before trim start)
    if (m_trimStart > 0) {
        int startX = xFromPosition(m_trimStart);
        QRect leftRect(track.left(), track.top(),
                       startX - track.left(), track.height());
        painter.fillRect(leftRect, overlayColor);
    }

    // Right excluded region (after trim end)
    if (m_trimEnd >= 0 && m_trimEnd < duration()) {
        int endX = xFromPosition(m_trimEnd);
        QRect rightRect(endX, track.top(),
                        track.right() - endX, track.height());
        painter.fillRect(rightRect, overlayColor);
    }
}

void TrimTimeline::drawTrimHandles(QPainter &painter)
{
    if (duration() <= 0) {
        return;
    }

    QColor handleColor(0, 122, 255);  // macOS blue
    QColor handleBorder(255, 255, 255, 200);

    // Draw start handle
    {
        QRect handle = startHandleRect();
        painter.setPen(QPen(handleBorder, 1));
        painter.setBrush(handleColor);
        painter.drawRoundedRect(handle, 2, 2);

        // Draw grip lines
        painter.setPen(QPen(Qt::white, 1));
        int cx = handle.center().x();
        int cy = handle.center().y();
        painter.drawLine(cx - 1, cy - 4, cx - 1, cy + 4);
        painter.drawLine(cx + 1, cy - 4, cx + 1, cy + 4);
    }

    // Draw end handle
    {
        QRect handle = endHandleRect();
        painter.setPen(QPen(handleBorder, 1));
        painter.setBrush(handleColor);
        painter.drawRoundedRect(handle, 2, 2);

        // Draw grip lines
        painter.setPen(QPen(Qt::white, 1));
        int cx = handle.center().x();
        int cy = handle.center().y();
        painter.drawLine(cx - 1, cy - 4, cx - 1, cy + 4);
        painter.drawLine(cx + 1, cy - 4, cx + 1, cy + 4);
    }
}

void TrimTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && duration() > 0) {
        m_dragTarget = hitTest(event->pos());

        if (m_dragTarget == DragTarget::StartHandle ||
            m_dragTarget == DragTarget::EndHandle) {
            // Handle drag - don't call base class
            event->accept();
            return;
        }

        // Playhead or track click - use base behavior
        m_dragTarget = DragTarget::Playhead;
    }

    VideoTimeline::mousePressEvent(event);
}

void TrimTimeline::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragTarget == DragTarget::StartHandle) {
        qint64 newStart = positionFromX(event->pos().x());
        // Ensure start doesn't go past end (leave at least 100ms)
        newStart = qMin(newStart, trimEnd() - 100);
        newStart = qMax(qint64(0), newStart);

        if (newStart != m_trimStart) {
            m_trimStart = newStart;
            update();
            emit trimRangeChanged(m_trimStart, trimEnd());
        }
        event->accept();
        return;
    }

    if (m_dragTarget == DragTarget::EndHandle) {
        qint64 newEnd = positionFromX(event->pos().x());
        // Ensure end doesn't go before start (leave at least 100ms)
        newEnd = qMax(newEnd, m_trimStart + 100);
        newEnd = qMin(newEnd, duration());

        qint64 effectiveEnd = (newEnd >= duration()) ? -1 : newEnd;
        if (m_trimEnd != effectiveEnd) {
            m_trimEnd = effectiveEnd;
            update();
            emit trimRangeChanged(m_trimStart, trimEnd());
        }
        event->accept();
        return;
    }

    // Default behavior for playhead
    VideoTimeline::mouseMoveEvent(event);
}

void TrimTimeline::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragTarget = DragTarget::None;
    }

    VideoTimeline::mouseReleaseEvent(event);
}

void TrimTimeline::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && duration() > 0) {
        DragTarget target = hitTest(event->pos());

        if (target == DragTarget::StartHandle) {
            emit trimHandleDoubleClicked(true);
            event->accept();
            return;
        }

        if (target == DragTarget::EndHandle) {
            emit trimHandleDoubleClicked(false);
            event->accept();
            return;
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}
