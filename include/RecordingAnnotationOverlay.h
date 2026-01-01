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
class LaserPointerRenderer;
class CursorHighlightEffect;
class SpotlightEffect;

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
     * @return True if successfully enabled (or disabled), false if failed
     *         (e.g., due to missing Accessibility permission on macOS)
     */
    bool setClickHighlightEnabled(bool enabled);

    /**
     * @brief Check if click highlight is enabled.
     */
    bool isClickHighlightEnabled() const;

    /**
     * @brief Enable or disable laser pointer effect.
     */
    void setLaserPointerEnabled(bool enabled);
    bool isLaserPointerEnabled() const;

    /**
     * @brief Enable or disable cursor highlight effect.
     */
    void setCursorHighlightEnabled(bool enabled);
    bool isCursorHighlightEnabled() const;

    /**
     * @brief Enable or disable spotlight effect.
     */
    void setSpotlightEnabled(bool enabled);
    bool isSpotlightEnabled() const;

    /**
     * @brief Set whether visual effects should be embedded in the recorded video.
     * When true, laser pointer, cursor highlight, and spotlight are composited onto frames.
     * When false, effects display live but are not recorded.
     */
    void setCompositeIndicatorsToVideo(bool enabled);
    bool compositeIndicatorsToVideo() const;

    /**
     * @brief Set laser pointer color.
     */
    void setLaserColor(const QColor &color);

    /**
     * @brief Set cursor highlight color.
     */
    void setCursorHighlightColor(const QColor &color);

    /**
     * @brief Set cursor highlight radius.
     */
    void setCursorHighlightRadius(int radius);

    /**
     * @brief Set spotlight follow radius.
     */
    void setSpotlightRadius(int radius);

    /**
     * @brief Set spotlight dim opacity.
     */
    void setSpotlightDimOpacity(qreal opacity);

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
    void onEffectNeedsRepaint();

private:
    void setupWindow();
    void invalidateCache();
    void updateCursorEffects(const QPoint &localPos);

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

    // Visual effects
    LaserPointerRenderer *m_laserRenderer;
    CursorHighlightEffect *m_cursorHighlight;
    SpotlightEffect *m_spotlightEffect;
    bool m_laserPointerEnabled = false;
    bool m_compositeIndicatorsToVideo = false;
};

#endif // RECORDINGANNOTATIONOVERLAY_H
