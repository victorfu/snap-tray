#ifndef RECORDINGANNOTATIONOVERLAY_H
#define RECORDINGANNOTATIONOVERLAY_H

#include <QWidget>
#include <QImage>
#include <QColor>
#include <QPoint>

class ClickRippleRenderer;
class MouseClickTracker;
class LaserPointerRenderer;
class CursorHighlightEffect;
class SpotlightEffect;

/**
 * @brief Transparent overlay for cursor effects during recording.
 *
 * This widget sits on top of the recording region and displays
 * cursor-following effects (spotlight, cursor highlight, laser pointer,
 * click ripples). These effects are composited onto captured frames
 * before encoding.
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
     * @brief Composite cursor effects onto a captured frame.
     *
     * This is called from RecordingManager::captureFrame() to
     * overlay cursor effects onto the captured screen content.
     *
     * @param frame The captured frame to composite onto (modified in place)
     * @param scale Device pixel ratio for coordinate scaling
     */
    void compositeOntoFrame(QImage &frame, qreal scale) const;

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

protected:
    void enterEvent(QEnterEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void onMouseClicked(QPoint globalPos);
    void onClickRippleNeedsRepaint();
    void onEffectNeedsRepaint();

private:
    void setupWindow();
    void invalidateCache();
    void updateCursorEffects(const QPoint &localPos);

    QRect m_region;

    // Click ripple cache for efficient frame compositing
    mutable QImage m_rippleCache;
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
