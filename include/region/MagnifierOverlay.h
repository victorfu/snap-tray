#ifndef MAGNIFIEROVERLAY_H
#define MAGNIFIEROVERLAY_H

#include <QPoint>
#include <QRect>
#include <QWidget>

#include "region/SelectionDirtyRegionPlanner.h"
#include "settings/RegionCaptureSettingsManager.h"

class MagnifierPanel;
class QPixmap;
class QPainter;

/**
 * @brief Transparent top-level window that renders the RegionSelector magnifier.
 *
 * The RegionSelector toolbar became a detached top-level QQuickView in
 * fdac97c3721e5d02710a2796e4295d873e32c840, which changed the visual stacking
 * order compared to the old in-window painter path. This overlay restores the
 * previous UX by rendering the magnifier above detached floating toolbars while
 * keeping the magnifier appearance and positioning unchanged.
 */
class MagnifierOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit MagnifierOverlay(MagnifierPanel* panel, QWidget* parent = nullptr);
    ~MagnifierOverlay() override = default;

    void syncToHost(QWidget* host,
                    const QPoint& cursorPos,
                    const QPixmap* backgroundPixmap,
                    RegionCaptureSettingsManager::CursorCompanionStyle style,
                    bool shouldShow);
    void hideOverlay();
    bool hasPaintedSinceShow() const { return m_hasPaintedSinceShow; }
    void paintFallback(QPainter& painter);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QRect hostGlobalRect() const;
    QPoint currentHostCursorPos() const;
    void drawBeaver(QPainter& painter);
    void paintCurrentStyle(QPainter& painter);

    MagnifierPanel* m_panel = nullptr;
    QWidget* m_host = nullptr;
    const QPixmap* m_backgroundPixmap = nullptr;
    QPoint m_cursorPos;
    QRect m_lastMagnifierRect;
    RegionCaptureSettingsManager::CursorCompanionStyle m_style =
        RegionCaptureSettingsManager::CursorCompanionStyle::Magnifier;
    QPixmap m_beaverPixmap;
    SelectionDirtyRegionPlanner m_dirtyRegionPlanner;
    bool m_hasPaintedSinceShow = false;
};

#endif // MAGNIFIEROVERLAY_H
