#pragma once

#include <QObject>
#include <QRect>
#include <QPointer>
#include <QWindow>  // for WId

class QQuickView;
class QQuickItem;
class QScreen;

namespace SnapTray {

/**
 * @brief QML-based scroll capture control bar.
 *
 * Drop-in replacement for ScrollCaptureControlBar (QWidget-based).
 * Uses QQuickView to render a floating Done/Cancel bar with
 * GPU-accelerated animations and drag support.
 *
 * The window is frameless, transparent, stays on top, and shows
 * without activating. It does NOT steal focus but accepts mouse
 * input for buttons and dragging.
 *
 * Usage:
 *   auto* bar = new QmlScrollControlBar();
 *   bar->positionNear(captureRegion, screen);
 *   bar->show();
 */
class QmlScrollControlBar : public QObject
{
    Q_OBJECT

public:
    explicit QmlScrollControlBar(QObject* parent = nullptr);
    ~QmlScrollControlBar() override;

    /**
     * @brief Position the bar near the capture region.
     * Tries bottom-center inside, then below, above, and screen bottom.
     * Skipped if the user has manually dragged the bar.
     */
    void positionNear(const QRect& captureRegion, QScreen* screen);

    /**
     * @brief Set the slow-scroll warning state (pulses the Done button ring).
     */
    void setSlowScrollWarning(bool active);

    /**
     * @brief Whether the user has manually dragged the bar.
     */
    bool hasManualPosition() const;

    void show();
    void hide();
    void close();
    WId winId() const;

    QQuickView* quickView() const { return m_view; }

signals:
    void finishRequested();
    void cancelRequested();
    void escapePressed();

private:
    void ensureView();
    void applyPlatformWindowFlags();
    void connectSignals();

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    bool m_manualPositionSet = false;
};

} // namespace SnapTray
