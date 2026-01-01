#include "video/AnnotationTimelineWidget.h"
#include "video/AnnotationTrack.h"
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

AnnotationTimelineWidget::AnnotationTimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(m_rowHeight + 4);

    m_contextMenu = new QMenu(this);
    m_contextMenu->addAction("Delete", [this]() {
        if (!m_hoverTarget.annotationId.isEmpty() && m_track) {
            m_track->removeAnnotation(m_hoverTarget.annotationId);
        }
    });
    m_contextMenu->addAction("Duplicate", [this]() {
        if (!m_hoverTarget.annotationId.isEmpty() && m_track) {
            const VideoAnnotation *ann = m_track->annotation(m_hoverTarget.annotationId);
            if (ann) {
                VideoAnnotation copy = *ann;
                copy.id = VideoAnnotation::generateId();
                copy.startTimeMs += 500; // Offset by 500ms
                if (copy.endTimeMs >= 0) {
                    copy.endTimeMs += 500;
                }
                m_track->addAnnotation(copy);
            }
        }
    });
    m_contextMenu->addSeparator();
    m_contextMenu->addAction("Bring to Front", [this]() {
        if (!m_hoverTarget.annotationId.isEmpty() && m_track) {
            m_track->bringToFront(m_hoverTarget.annotationId);
        }
    });
    m_contextMenu->addAction("Send to Back", [this]() {
        if (!m_hoverTarget.annotationId.isEmpty() && m_track) {
            m_track->sendToBack(m_hoverTarget.annotationId);
        }
    });
}

AnnotationTimelineWidget::~AnnotationTimelineWidget() = default;

void AnnotationTimelineWidget::setTrack(AnnotationTrack *track)
{
    if (m_track) {
        disconnect(m_track, nullptr, this, nullptr);
    }

    m_track = track;

    if (m_track) {
        connect(m_track, &AnnotationTrack::annotationAdded, this, [this]() {
            recalculateLayout();
            update();
        });
        connect(m_track, &AnnotationTrack::annotationRemoved, this, [this]() {
            recalculateLayout();
            update();
        });
        connect(m_track, &AnnotationTrack::annotationUpdated, this, [this]() {
            recalculateLayout();
            update();
        });
        connect(m_track, &AnnotationTrack::selectionChanged, this, [this]() { update(); });
    }

    recalculateLayout();
    update();
}

AnnotationTrack *AnnotationTimelineWidget::track() const
{
    return m_track;
}

void AnnotationTimelineWidget::setDuration(qint64 durationMs)
{
    if (m_durationMs != durationMs) {
        m_durationMs = durationMs;
        recalculateLayout();
        update();
    }
}

qint64 AnnotationTimelineWidget::duration() const
{
    return m_durationMs;
}

void AnnotationTimelineWidget::setCurrentTime(qint64 timeMs)
{
    if (m_currentTimeMs != timeMs) {
        m_currentTimeMs = timeMs;
        update();
    }
}

qint64 AnnotationTimelineWidget::currentTime() const
{
    return m_currentTimeMs;
}

void AnnotationTimelineWidget::setRowHeight(int height)
{
    m_rowHeight = qBound(16, height, 48);
    recalculateLayout();
    update();
}

int AnnotationTimelineWidget::rowHeight() const
{
    return m_rowHeight;
}

void AnnotationTimelineWidget::setBarSpacing(int spacing)
{
    m_barSpacing = qBound(0, spacing, 8);
    recalculateLayout();
    update();
}

int AnnotationTimelineWidget::barSpacing() const
{
    return m_barSpacing;
}

void AnnotationTimelineWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Background
    painter.fillRect(rect(), QColor(40, 40, 40));

    if (m_durationMs <= 0 || !m_track) {
        return;
    }

    // Draw row separators
    painter.setPen(QColor(60, 60, 60));
    for (int row = 1; row < m_rowCount; ++row) {
        int y = row * (m_rowHeight + m_barSpacing);
        painter.drawLine(0, y, width(), y);
    }

    // Draw annotation bars
    QString selectedId = m_track->selectedId();

    for (const auto &item : m_layout) {
        const VideoAnnotation *ann = m_track->annotation(item.annotationId);
        if (!ann) {
            continue;
        }

        bool isSelected = (item.annotationId == selectedId);
        bool isHovered = (item.annotationId == m_hoverTarget.annotationId);

        QColor baseColor = colorForAnnotation(*ann);
        QColor fillColor = baseColor;
        QColor borderColor = baseColor.darker(120);

        if (isSelected) {
            borderColor = QColor(0, 120, 215);
        } else if (isHovered) {
            fillColor = fillColor.lighter(115);
        }

        // Draw bar
        QRect barRect = item.rect;
        painter.setPen(QPen(borderColor, isSelected ? 2 : 1));
        painter.setBrush(fillColor);
        painter.drawRoundedRect(barRect, 3, 3);

        // Draw handles when hovered or selected
        if (isSelected || isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, 180));

            QRect leftHandle(barRect.left(), barRect.top() + 2, m_handleWidth, barRect.height() - 4);
            QRect rightHandle(barRect.right() - m_handleWidth, barRect.top() + 2, m_handleWidth,
                              barRect.height() - 4);

            painter.drawRoundedRect(leftHandle, 2, 2);
            painter.drawRoundedRect(rightHandle, 2, 2);
        }

        // Draw annotation type indicator/label
        QString label;
        switch (ann->type) {
        case VideoAnnotationType::Arrow:
            label = "→";
            break;
        case VideoAnnotationType::Line:
            label = "—";
            break;
        case VideoAnnotationType::Rectangle:
            label = "□";
            break;
        case VideoAnnotationType::Ellipse:
            label = "○";
            break;
        case VideoAnnotationType::Pencil:
            label = "✎";
            break;
        case VideoAnnotationType::Marker:
            label = "▬";
            break;
        case VideoAnnotationType::Text:
            label = "T";
            break;
        case VideoAnnotationType::StepBadge:
            label = QString::number(ann->stepNumber);
            break;
        case VideoAnnotationType::Blur:
            label = "▒";
            break;
        case VideoAnnotationType::Highlight:
            label = "▓";
            break;
        }

        if (barRect.width() > 24) {
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setPointSize(10);
            painter.setFont(font);
            painter.drawText(barRect.adjusted(m_handleWidth + 4, 0, -m_handleWidth - 4, 0),
                             Qt::AlignVCenter | Qt::AlignLeft, label);
        }
    }

    // Draw playhead
    int playheadX = timeToX(m_currentTimeMs);
    painter.setPen(QPen(QColor(255, 80, 80), 2));
    painter.drawLine(playheadX, 0, playheadX, height());
}

void AnnotationTimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        HitResult hit = hitTest(event->pos());

        if (hit.area != HitArea::None) {
            m_isDragging = true;
            m_dragTarget = hit;
            m_dragStartX = event->pos().x();

            const VideoAnnotation *ann = m_track->annotation(hit.annotationId);
            if (ann) {
                m_dragStartTimeMs = ann->startTimeMs;
                m_dragEndTimeMs = ann->endTimeMs;
            }

            if (m_track) {
                m_track->setSelectedId(hit.annotationId);
            }
            emit selectionChanged(hit.annotationId);
        } else {
            // Clicked empty area - seek to that time
            qint64 time = xToTime(event->pos().x());
            emit seekRequested(time);

            if (m_track) {
                m_track->clearSelection();
            }
            emit selectionChanged(QString());
        }

        update();
    }

    QWidget::mousePressEvent(event);
}

void AnnotationTimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && m_track) {
        int deltaX = event->pos().x() - m_dragStartX;
        qint64 deltaTime = xToTime(m_dragStartX + deltaX) - xToTime(m_dragStartX);

        VideoAnnotation *ann = m_track->annotation(m_dragTarget.annotationId);
        if (ann) {
            VideoAnnotation updated = *ann;

            switch (m_dragTarget.area) {
            case HitArea::Body:
                // Move entire annotation
                updated.startTimeMs = qMax(0LL, m_dragStartTimeMs + deltaTime);
                if (m_dragEndTimeMs >= 0) {
                    updated.endTimeMs = m_dragEndTimeMs + deltaTime;
                }
                break;

            case HitArea::LeftHandle:
                // Adjust start time
                updated.startTimeMs = qBound(0LL, m_dragStartTimeMs + deltaTime,
                                             m_dragEndTimeMs >= 0 ? m_dragEndTimeMs - 100
                                                                  : m_durationMs - 100);
                break;

            case HitArea::RightHandle:
                // Adjust end time
                if (m_dragEndTimeMs >= 0) {
                    updated.endTimeMs =
                        qMax(m_dragStartTimeMs + 100, m_dragEndTimeMs + deltaTime);
                } else {
                    // Changing from "until end" to specific time
                    updated.endTimeMs = qMax(updated.startTimeMs + 100, xToTime(event->pos().x()));
                }
                break;

            default:
                break;
            }

            // Direct update to avoid undo stack during drag
            // We'll commit on mouse release
            ann->startTimeMs = updated.startTimeMs;
            ann->endTimeMs = updated.endTimeMs;
            recalculateLayout();
            update();
            // Emit signal for live preview update
            emit timingDragging(m_dragTarget.annotationId, ann->startTimeMs, ann->endTimeMs);
        }
    } else {
        // Update hover state
        HitResult hit = hitTest(event->pos());
        if (hit.annotationId != m_hoverTarget.annotationId || hit.area != m_hoverTarget.area) {
            m_hoverTarget = hit;
            update();

            // Update cursor
            if (hit.area == HitArea::LeftHandle || hit.area == HitArea::RightHandle) {
                setCursor(Qt::SizeHorCursor);
            } else if (hit.area == HitArea::Body) {
                setCursor(Qt::OpenHandCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
        }
    }

    QWidget::mouseMoveEvent(event);
}

void AnnotationTimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDragging && m_track) {
        m_isDragging = false;

        // Commit the timing change to undo stack
        VideoAnnotation *ann = m_track->annotation(m_dragTarget.annotationId);
        if (ann) {
            // Only commit if timing actually changed
            if (ann->startTimeMs != m_dragStartTimeMs || ann->endTimeMs != m_dragEndTimeMs) {
                emit timingChanged(m_dragTarget.annotationId, ann->startTimeMs, ann->endTimeMs);
            }
        }

        m_dragTarget = HitResult();
    }

    QWidget::mouseReleaseEvent(event);
}

void AnnotationTimelineWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    HitResult hit = hitTest(event->pos());
    if (hit.area != HitArea::None && m_track) {
        const VideoAnnotation *ann = m_track->annotation(hit.annotationId);
        if (ann) {
            emit seekRequested(ann->startTimeMs);
        }
    }

    QWidget::mouseDoubleClickEvent(event);
}

void AnnotationTimelineWidget::contextMenuEvent(QContextMenuEvent *event)
{
    HitResult hit = hitTest(event->pos());
    if (hit.area != HitArea::None) {
        m_hoverTarget = hit;
        if (m_track) {
            m_track->setSelectedId(hit.annotationId);
        }
        m_contextMenu->popup(event->globalPos());
    }
}

QSize AnnotationTimelineWidget::sizeHint() const
{
    int rows = qMax(1, m_rowCount);
    return QSize(200, rows * (m_rowHeight + m_barSpacing) + m_barSpacing);
}

QSize AnnotationTimelineWidget::minimumSizeHint() const
{
    return QSize(100, m_rowHeight + 4);
}

void AnnotationTimelineWidget::recalculateLayout()
{
    m_layout.clear();
    m_rowCount = 0;

    if (!m_track || m_durationMs <= 0) {
        updateGeometry();
        return;
    }

    QList<VideoAnnotation> annotations = m_track->allAnnotations();

    // Sort by start time
    std::sort(annotations.begin(), annotations.end(),
              [](const VideoAnnotation &a, const VideoAnnotation &b) {
                  return a.startTimeMs < b.startTimeMs;
              });

    // Track end times for each row to avoid overlaps
    QVector<qint64> rowEndTimes;

    for (const auto &ann : annotations) {
        qint64 endTime = ann.endTimeMs >= 0 ? ann.endTimeMs : m_durationMs;

        // Find first row where this annotation fits
        int row = -1;
        for (int r = 0; r < rowEndTimes.size(); ++r) {
            if (rowEndTimes[r] <= ann.startTimeMs) {
                row = r;
                break;
            }
        }

        if (row < 0) {
            row = rowEndTimes.size();
            rowEndTimes.append(0);
        }

        rowEndTimes[row] = endTime;

        // Calculate rect
        int x1 = timeToX(ann.startTimeMs);
        int x2 = timeToX(endTime);
        int y = m_barSpacing + row * (m_rowHeight + m_barSpacing);

        LayoutRow item;
        item.annotationId = ann.id;
        item.row = row;
        item.rect = QRect(x1, y, qMax(20, x2 - x1), m_rowHeight);
        m_layout.append(item);
    }

    m_rowCount = rowEndTimes.size();
    updateGeometry();
}

AnnotationTimelineWidget::HitResult AnnotationTimelineWidget::hitTest(const QPoint &pos) const
{
    HitResult result;

    for (const auto &item : m_layout) {
        if (item.rect.contains(pos)) {
            result.annotationId = item.annotationId;
            result.row = item.row;

            // Check if on handle
            if (pos.x() <= item.rect.left() + m_handleWidth) {
                result.area = HitArea::LeftHandle;
            } else if (pos.x() >= item.rect.right() - m_handleWidth) {
                result.area = HitArea::RightHandle;
            } else {
                result.area = HitArea::Body;
            }
            break;
        }
    }

    return result;
}

int AnnotationTimelineWidget::timeToX(qint64 timeMs) const
{
    if (m_durationMs <= 0) {
        return m_leftMargin;
    }

    int availableWidth = width() - m_leftMargin - m_rightMargin;
    return m_leftMargin + static_cast<int>(timeMs * availableWidth / m_durationMs);
}

qint64 AnnotationTimelineWidget::xToTime(int x) const
{
    if (m_durationMs <= 0) {
        return 0;
    }

    int availableWidth = width() - m_leftMargin - m_rightMargin;
    if (availableWidth <= 0) {
        return 0;
    }

    return static_cast<qint64>((x - m_leftMargin) * m_durationMs / availableWidth);
}

QColor AnnotationTimelineWidget::colorForAnnotation(const VideoAnnotation &ann) const
{
    // Use annotation color but ensure reasonable visibility
    QColor color = ann.color;
    if (color.lightnessF() < 0.3) {
        color = color.lighter(150);
    }
    return color;
}

void AnnotationTimelineWidget::showContextMenu(const QString &annotationId,
                                               const QPoint &globalPos)
{
    emit contextMenuRequested(annotationId, globalPos);
}
