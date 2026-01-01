#ifndef ANNOTATION_TIMELINE_WIDGET_H
#define ANNOTATION_TIMELINE_WIDGET_H

#include <QMenu>
#include <QWidget>

class AnnotationTrack;
struct VideoAnnotation;

class AnnotationTimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnnotationTimelineWidget(QWidget *parent = nullptr);
    ~AnnotationTimelineWidget() override;

    void setTrack(AnnotationTrack *track);
    AnnotationTrack *track() const;

    void setDuration(qint64 durationMs);
    qint64 duration() const;

    void setCurrentTime(qint64 timeMs);
    qint64 currentTime() const;

    // Visual settings
    void setRowHeight(int height);
    int rowHeight() const;

    void setBarSpacing(int spacing);
    int barSpacing() const;

signals:
    void selectionChanged(const QString &annotationId);
    void timingChanged(const QString &annotationId, qint64 startMs, qint64 endMs);
    void timingDragging(const QString &annotationId, qint64 startMs, qint64 endMs);
    void seekRequested(qint64 timeMs);
    void contextMenuRequested(const QString &annotationId, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    enum class HitArea { None, Body, LeftHandle, RightHandle };

    struct HitResult {
        QString annotationId;
        HitArea area = HitArea::None;
        int row = -1;
    };

    struct LayoutRow {
        QString annotationId;
        int row;
        QRect rect;
    };

    void recalculateLayout();
    HitResult hitTest(const QPoint &pos) const;
    int timeToX(qint64 timeMs) const;
    qint64 xToTime(int x) const;
    QColor colorForAnnotation(const VideoAnnotation &ann) const;
    void showContextMenu(const QString &annotationId, const QPoint &globalPos);

    AnnotationTrack *m_track = nullptr;
    qint64 m_durationMs = 0;
    qint64 m_currentTimeMs = 0;

    int m_rowHeight = 24;
    int m_barSpacing = 2;
    int m_handleWidth = 8;
    int m_leftMargin = 0;
    int m_rightMargin = 0;

    QVector<LayoutRow> m_layout;
    int m_rowCount = 0;

    // Drag state
    bool m_isDragging = false;
    HitResult m_dragTarget;
    qint64 m_dragStartTimeMs = 0;
    qint64 m_dragEndTimeMs = 0;
    int m_dragStartX = 0;

    // Hover state
    HitResult m_hoverTarget;

    QMenu *m_contextMenu = nullptr;
};

#endif // ANNOTATION_TIMELINE_WIDGET_H
