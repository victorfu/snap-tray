#pragma once

#include <QObject>
#include <QRect>
#include <QColor>
#include <QPointer>
#include <QWindow>  // for WId

class QQuickView;
class QQuickItem;
class QShortcut;

namespace SnapTray {
class GlassTooltip;

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
 *   - GlassTooltip on button hover
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
    void onButtonHovered(int buttonId, qreal anchorX, qreal anchorY,
                         qreal anchorW, qreal anchorH);
    void onButtonUnhovered();
    void onDragStarted();
    void onDragFinished();
    void onDragMoved(qreal deltaX, qreal deltaY);
    void onWidthChanged();

private:
    void ensureView();
    void applyPlatformWindowFlags();
    void setupConnections();
    void updateThemeColors();
    QString formatDuration(qint64 ms) const;

    void showTooltip(const QString& text, const QRect& anchorRect);
    void hideTooltip();
    QString tooltipForButton(int buttonId) const;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QShortcut* m_escShortcut = nullptr;
    GlassTooltip* m_tooltip = nullptr;

    QRect m_recordingRegion;
    bool m_isPaused = false;

    // For drag: track where the view started
    QPoint m_dragStartViewPos;

    // For re-centering after width change
    bool m_isDragging = false;
};

} // namespace SnapTray
