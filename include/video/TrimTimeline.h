#ifndef TRIMTIMELINE_H
#define TRIMTIMELINE_H

#include "video/VideoTimeline.h"

/**
 * @brief Extended timeline widget with trim handles for video editing.
 *
 * Adds start and end trim handles to the base VideoTimeline, allowing
 * users to select a portion of the video to keep. The trim region is
 * visualized with an overlay showing excluded areas.
 */
class TrimTimeline : public VideoTimeline
{
    Q_OBJECT

public:
    explicit TrimTimeline(QWidget *parent = nullptr);

    /**
     * @brief Set the trim range.
     * @param startMs Trim start in milliseconds (-1 for beginning)
     * @param endMs Trim end in milliseconds (-1 for end)
     */
    void setTrimRange(qint64 startMs, qint64 endMs);

    /**
     * @brief Get trim start position.
     * @return Trim start in ms, or 0 if at beginning
     */
    qint64 trimStart() const { return m_trimStart; }

    /**
     * @brief Get trim end position.
     * @return Trim end in ms, or duration if at end
     */
    qint64 trimEnd() const;

    /**
     * @brief Get the duration of the trimmed region.
     */
    qint64 trimmedDuration() const;

    /**
     * @brief Check if any trimming is applied.
     */
    bool hasTrim() const;

signals:
    /**
     * @brief Emitted when the trim range changes.
     */
    void trimRangeChanged(qint64 startMs, qint64 endMs);

    /**
     * @brief Emitted when a trim handle is double-clicked for precise input.
     * @param isStartHandle True for start handle, false for end handle
     */
    void trimHandleDoubleClicked(bool isStartHandle);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    enum class DragTarget {
        None,
        StartHandle,
        EndHandle,
        Playhead
    };

    QRect startHandleRect() const;
    QRect endHandleRect() const;
    DragTarget hitTest(const QPoint &pos) const;
    void drawTrimOverlay(QPainter &painter);
    void drawTrimHandles(QPainter &painter);

    qint64 m_trimStart = 0;
    qint64 m_trimEnd = -1;  // -1 means end of video
    DragTarget m_dragTarget = DragTarget::None;

    // Handle dimensions
    static constexpr int kHandleWidth = 8;
    static constexpr int kHandleHeight = 24;
};

#endif // TRIMTIMELINE_H
