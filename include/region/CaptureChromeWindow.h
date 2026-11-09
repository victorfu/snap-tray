#ifndef CAPTURECHROMEWINDOW_H
#define CAPTURECHROMEWINDOW_H

#include "region/RegionPainter.h"

#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QWidget>

class AnnotationLayer;
class CaptureShortcutHintsOverlay;
class LoadingSpinnerRenderer;
class SelectionStateManager;
class ToolManager;

namespace snaptray::region {

enum class CaptureChromeDirtyReason {
    Scene,
    PencilPreview
};

CaptureChromeDirtyReason captureChromeDirtyReasonForToolPreview(
    bool pencilToolActive,
    const QRegion& previewDirtyRegion);

struct CaptureChromePendingDirtyRegions
{
    QRegion scene;
    QRegion pencilPreview;

    void add(const QRegion& region, CaptureChromeDirtyReason reason);
    void clear();
    CaptureChromePendingDirtyRegions take();
};

struct CaptureChromeDirtyRegionPlanInput
{
    QRect windowRect;
    QRegion sceneDirtyRegion;
    QRegion previewDirtyRegion;
    QRegion stateDirtyRegion;
    bool forceFullRepaint = false;
    bool renderStateChanged = false;
    bool conservativeStateCompositing = false;
};

QRegion planCaptureChromeDirtyRegion(const CaptureChromeDirtyRegionPlanInput& input);
QRegion planCaptureChromeDirtyRegionForCurrentPlatform(
    CaptureChromeDirtyRegionPlanInput input);

} // namespace snaptray::region

class CaptureChromeWindow : public QWidget
{
    Q_OBJECT

public:
    explicit CaptureChromeWindow(QWidget* parent = nullptr);
    ~CaptureChromeWindow() override = default;

    void setSelectionManager(SelectionStateManager* manager);
    void setAnnotationLayer(AnnotationLayer* layer);
    void setToolManager(ToolManager* manager);
    void setShortcutHintsOverlay(CaptureShortcutHintsOverlay* overlay);
    void markDirtyRegion(
        const QRegion& region,
        snaptray::region::CaptureChromeDirtyReason reason =
            snaptray::region::CaptureChromeDirtyReason::Scene);

    void syncToHost(QWidget* host,
                    const QRect& selectionRect,
                    bool hasActiveSelection,
                    const QRect& highlightedWindowRect,
                    qreal devicePixelRatio,
                    int cornerRadius,
                    int currentTool,
                    bool showShortcutHints,
                    LoadingSpinnerRenderer* loadingSpinner,
                    bool showBusySpinner,
                    const QPoint& busySpinnerCenter,
                    bool shouldShow);
    void hideOverlay();

    QRect lastDimensionInfoRect() const { return m_lastDimensionInfoRect; }

signals:
    void framePainted();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QWidget* m_host = nullptr;
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
    CaptureShortcutHintsOverlay* m_shortcutHintsOverlay = nullptr;
    RegionPainter m_regionPainter;
    QRect m_selectionRect;
    bool m_hasActiveSelection = false;
    QRect m_highlightedWindowRect;
    qreal m_devicePixelRatio = 1.0;
    int m_cornerRadius = 0;
    int m_currentTool = 0;
    bool m_showShortcutHints = false;
    LoadingSpinnerRenderer* m_loadingSpinner = nullptr;
    bool m_showBusySpinner = false;
    QPoint m_busySpinnerCenter;
    QRect m_lastDimensionInfoRect;
    bool m_useNativeLayeredBackend = false;
    snaptray::region::CaptureChromePendingDirtyRegions m_pendingDirtyRegions;
};

#endif // CAPTURECHROMEWINDOW_H
