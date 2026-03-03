#pragma once

#include <QObject>
#include <QRect>
#include <QPointer>
#include <QWindow>  // for WId

class QQuickView;
class QQuickItem;

namespace SnapTray {

/**
 * @brief QML-based recording boundary overlay.
 *
 * Drop-in replacement for RecordingBoundaryOverlay (QWidget-based).
 * Uses QQuickView to render an animated border around the recording region
 * with GPU-accelerated animations.
 *
 * Three border modes:
 *   - Recording: rotating conical gradient (blue/indigo/purple)
 *   - Playing:   pulsing green
 *   - Paused:    static amber
 *
 * The window is frameless, transparent, click-through, and stays on top.
 *
 * Usage:
 *   auto* boundary = new QmlRecordingBoundary();
 *   boundary->setRegion(recordingRegion);
 *   boundary->show();
 */
class QmlRecordingBoundary : public QObject
{
    Q_OBJECT

public:
    enum class BorderMode {
        Recording = 0,
        Playing   = 1,
        Paused    = 2
    };

    explicit QmlRecordingBoundary(QObject* parent = nullptr);
    ~QmlRecordingBoundary() override;

    /**
     * @brief Set the recording region. Positions and sizes the overlay window.
     */
    void setRegion(const QRect& region);

    /**
     * @brief Set the border animation mode.
     */
    void setBorderMode(BorderMode mode);
    BorderMode borderMode() const { return m_borderMode; }

    /**
     * @brief Show the overlay window.
     */
    void show();

    /**
     * @brief Hide the overlay window.
     */
    void hide();

    /**
     * @brief Close and destroy the overlay window.
     */
    void close();

    /**
     * @brief Get the native window ID (for capture exclusion).
     */
    WId winId() const;

    /**
     * @brief Raise the window above the menu bar (macOS).
     * Call after show() to ensure the native window exists.
     */
    void raiseAboveMenuBar();

    /**
     * @brief Exclude this window from screen capture.
     */
    void setExcludedFromCapture(bool excluded);

    /**
     * @brief Get the underlying QQuickView (for platform-specific operations).
     * Returns nullptr before show() is called.
     */
    QQuickView* quickView() const { return m_view; }

private:
    void ensureView();
    void applyPlatformWindowFlags();

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QRect m_region;
    BorderMode m_borderMode = BorderMode::Recording;

    static constexpr int kBorderWidth = 4;
    static constexpr int kGlowPadding = 10;
};

} // namespace SnapTray
