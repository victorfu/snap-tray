#pragma once

#include <QObject>
#include <QRect>
#include <QPointer>
#include <QWindow>  // for WId

class QQuickView;
class QQuickItem;
class QShortcut;

namespace SnapTray {

/**
 * @brief QML-based recording control bar.
 *
 * Drop-in replacement for RecordingControlBar (QWidget-based).
 * Uses QQuickView to render the control bar with GPU-accelerated rendering.
 *
 * Features:
 *   - Recording indicator with animated gradient (recording/paused/preparing)
 *   - Duration, region size, FPS display
 *   - Pause/Resume, Stop, Cancel buttons
 *   - Glass effect background
 *   - Drag to reposition
 *   - QML tooltip on button hover
 *
 * Usage:
 *   auto* bar = new QmlRecordingControlBar();
 *   bar->show();
 *   bar->positionNear(recordingRegion);
 */
class QmlRecordingControlBar : public QObject
{
    Q_OBJECT

public:
    explicit QmlRecordingControlBar(QObject* parent = nullptr);
    ~QmlRecordingControlBar() override;

    void show();
    void hide();
    void close();

    WId winId() const;
    void raiseAboveMenuBar();
    void setExcludedFromCapture(bool excluded);

    void positionNear(const QRect& recordingRegion);
    void updateDuration(qint64 elapsedMs);
    void setPaused(bool paused);
    void setPreparing(bool preparing);
    void setPreparingStatus(const QString& status);
    void updateRegionSize(int width, int height);
    void updateFps(double fps);
    void setAudioEnabled(bool enabled);

signals:
    void stopRequested();
    void cancelRequested();
    void pauseRequested();
    void resumeRequested();

private slots:
    void onButtonHovered(int buttonId, double anchorX, double anchorY,
                         double anchorW, double anchorH);
    void onButtonUnhovered();
    void onDragStarted();
    void onDragFinished();
    void onDragMoved(double deltaX, double deltaY);
    void onWidthChanged();

private:
    void ensureView();
    void ensureTooltipView();
    void applyPlatformWindowFlags();
    void applyTooltipWindowFlags();
    void setupConnections();
    QString formatDuration(qint64 ms) const;

    void showTooltip(const QString& text, const QRect& anchorRect);
    void hideTooltip();
    QString tooltipForButton(int buttonId) const;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QShortcut* m_escShortcut = nullptr;
    QQuickView* m_tooltipView = nullptr;
    QQuickItem* m_tooltipRootItem = nullptr;

    QRect m_recordingRegion;
    bool m_isPaused = false;

    // For drag: track where the view started
    QPoint m_dragStartViewPos;
    QPoint m_dragStartCursorPos;

    // For re-centering after width change
    bool m_isDragging = false;
    quint64 m_tooltipRequestId = 0;
};

} // namespace SnapTray
