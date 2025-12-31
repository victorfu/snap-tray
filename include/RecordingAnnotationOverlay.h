#ifndef RECORDINGANNOTATIONOVERLAY_H
#define RECORDINGANNOTATIONOVERLAY_H

#include <QWidget>
#include <QImage>
#include <QColor>
#include <QPoint>

class AnnotationLayer;
class AnnotationController;
class ClickRippleRenderer;
class MouseClickTracker;

/**
 * @brief Transparent overlay for drawing annotations during recording.
 *
 * This widget sits on top of the recording region and allows users
 * to draw annotations in real-time. The annotations are composited
 * onto captured frames before encoding.
 */
class RecordingAnnotationOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingAnnotationOverlay(QWidget *parent = nullptr);
    ~RecordingAnnotationOverlay();

    /**
     * @brief Set the recording region in screen coordinates.
     */
    void setRegion(const QRect &region);

    /**
     * @brief Get the annotation layer for direct access.
     */
    AnnotationLayer* annotationLayer() { return m_annotationLayer; }

    /**
     * @brief Set the current annotation tool.
     */
    void setCurrentTool(int tool);

    /**
     * @brief Get the current tool.
     */
    int currentTool() const;

    /**
     * @brief Set the annotation color.
     */
    void setColor(const QColor &color);

    /**
     * @brief Get the current color.
     */
    QColor color() const;

    /**
     * @brief Set the annotation stroke width.
     */
    void setWidth(int width);

    /**
     * @brief Get the current stroke width.
     */
    int width() const;

    /**
     * @brief Composite annotations onto a captured frame.
     *
     * This is called from RecordingManager::captureFrame() to
     * overlay annotations onto the captured screen content.
     *
     * @param frame The captured frame to composite onto (modified in place)
     * @param scale Device pixel ratio for coordinate scaling
     */
    void compositeOntoFrame(QImage &frame, qreal scale) const;

    /**
     * @brief Clear all annotations.
     */
    void clear();

    /**
     * @brief Check if there are any annotations.
     */
    bool hasAnnotations() const;

    /**
     * @brief Undo the last annotation.
     */
    void undo();

    /**
     * @brief Redo the last undone annotation.
     */
    void redo();

    /**
     * @brief Enable or disable click highlight effect.
     * @param enabled True to show click ripples during recording
     */
    void setClickHighlightEnabled(bool enabled);

    /**
     * @brief Check if click highlight is enabled.
     */
    bool isClickHighlightEnabled() const;

signals:
    /**
     * @brief Emitted when annotations change.
     */
    void annotationChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onMouseClicked(QPoint globalPos);
    void onClickRippleNeedsRepaint();

private:
    void setupWindow();
    void invalidateCache();

    QRect m_region;
    AnnotationLayer *m_annotationLayer;
    AnnotationController *m_annotationController;

    // Annotation cache for efficient frame compositing
    mutable QImage m_annotationCache;
    mutable bool m_cacheInvalid;
    mutable QSize m_cacheSize;

    // Click highlight
    ClickRippleRenderer *m_clickRipple;
    MouseClickTracker *m_clickTracker;
};

#endif // RECORDINGANNOTATIONOVERLAY_H
