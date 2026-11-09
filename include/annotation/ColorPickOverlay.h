#pragma once

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <functional>

#include "region/SelectionDirtyRegionPlanner.h"

class MagnifierPanel;

/**
 * What an eyedropper samples (#271): the host's own image, never its
 * annotations or UI.
 *
 * Rects are global logical coordinates. The overlay window covers
 * overlayGeometry; image holds the device pixels of sampleRect and carries
 * the device pixel ratio that maps one onto the other.
 */
struct ColorPickSource {
    QRect overlayGeometry;
    QRect sampleRect;
    QImage image;

    bool isValid() const;
};

namespace ColorPick {

// Physical pixel of image under a logical point relative to the sample
// rect's top-left. Floors, so a fractional position at 2x scaling picks the
// pixel it lies on rather than a rounded neighbour. Returns (-1, -1) when the
// point lies outside the image.
QPoint pixelAt(const QPointF& logicalInSample, qreal devicePixelRatio, const QSize& imageSize);

// Moves a pixel by delta physical pixels, staying inside the image.
QPoint nudged(const QPoint& pixel, const QPoint& delta, const QSize& imageSize);

// The opaque color of one pixel, exactly as stored (no resampling, no color
// conversion: annotations are drawn into the same untagged buffer).
QColor colorAt(const QImage& image, const QPoint& pixel);

// "#1A73E8"
QString hexName(const QColor& color);

}  // namespace ColorPick

/**
 * Eyedropper mode (#271): a top-level window over the host that shows a
 * magnifier and picks one physical pixel of ColorPickSource::image.
 *
 * Left click (or press, drag, release) picks; arrow keys move the pick point
 * by one physical pixel and Enter picks it; Esc, right click, or a click
 * outside the sample area cancels. Emits exactly one of colorPicked or
 * cancelled, then closes; an overlay from begin() also deletes itself.
 */
class ColorPickOverlay : public QWidget
{
    Q_OBJECT

public:
    // Alpha of the near-invisible fill: fully transparent pixels let clicks
    // through to the windows below on Windows and macOS.
    static constexpr int kHitTestAlpha = 1;
    static constexpr int kReticleArm = 7;
    static constexpr int kReticleGap = 3;
    static constexpr int kReticlePenWidth = 3;

    explicit ColorPickOverlay(ColorPickSource source);
    ~ColorPickOverlay() override;

    // Starts picking over host; on success onPicked runs with the color, then
    // a screen reader hears "Color set to #RRGGBB". Focus returns to host,
    // except when the pick ended because another window took activation:
    // that window keeps focus. Returns nullptr when source is invalid.
    static ColorPickOverlay* begin(QWidget* host, ColorPickSource source,
                                   std::function<void(const QColor&)> onPicked);

    void cancel();

    bool hasSample() const { return m_hasSample; }
    QPoint samplePixel() const { return m_pixel; }
    QColor currentColor() const;
    // True once the pick was cancelled because the overlay lost activation.
    bool lostActivation() const { return m_lostActivation; }

signals:
    void colorPicked(const QColor& color);
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    void trackPointer(const QPointF& localPos);
    void setSample(bool hasSample, const QPoint& pixel);
    QPointF pixelCenterLocal() const;
    QRect paintedRect() const;  // magnifier and reticle; empty without a sample
    void finish(bool accepted);
    void syncCursor();

    ColorPickSource m_source;
    QPixmap m_samplePixmap;
    MagnifierPanel* m_magnifier = nullptr;
    SelectionDirtyRegionPlanner m_dirtyPlanner;
    QPoint m_pixel;
    bool m_hasSample = false;
    bool m_pressed = false;
    bool m_finished = false;
    bool m_wasActive = false;
    bool m_lostActivation = false;
    bool m_cursorHidden = false;
};
