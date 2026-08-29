#include "region/CaptureChromeWindow.h"

#include "region/CapturePerfRecorder.h"
#include "region/CaptureShortcutHintsOverlay.h"
#include "region/SelectionStateManager.h"
#include "annotations/AnnotationLayer.h"
#include "tools/ToolManager.h"
#include "LoadingSpinnerRenderer.h"
#include "platform/WindowLevel.h"

#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>

namespace {

constexpr int kActiveVisualRepaintPadding = 2;

bool preferNativeLayeredBackend()
{
#if defined(Q_OS_WIN) && !defined(QT_NO_DEBUG_OUTPUT)
    return qEnvironmentVariableIntValue("SNAPTRAY_CAPTURE_NATIVE_WINDOWS") > 0;
#else
    return false;
#endif
}

QRect activeVisualRect(RegionPainter& painter,
                       const QRect& selectionRect,
                       bool hasActiveSelection,
                       const QRect& highlightedWindowRect)
{
    const QRect activeRect = hasActiveSelection
        ? selectionRect.normalized()
        : highlightedWindowRect.normalized();
    if (!activeRect.isValid() || activeRect.isEmpty()) {
        return {};
    }

    const QRect visualRect = painter.getWindowHighlightVisualRect(activeRect);
    if (!visualRect.isValid() || visualRect.isEmpty()) {
        return visualRect;
    }

    return visualRect.adjusted(-kActiveVisualRepaintPadding,
                               -kActiveVisualRepaintPadding,
                               kActiveVisualRepaintPadding,
                               kActiveVisualRepaintPadding);
}

QRect spinnerRect(const LoadingSpinnerRenderer* spinner,
                  bool showBusySpinner,
                  const QPoint& center)
{
    if (!spinner || !showBusySpinner) {
        return {};
    }

    return spinner->bounds(center);
}

QRect shortcutHintsRect(const CaptureShortcutHintsOverlay* overlay,
                        bool showShortcutHints,
                        const QSize& viewportSize)
{
    if (!overlay || !showShortcutHints) {
        return {};
    }

    return overlay->repaintRectForViewport(viewportSize);
}

} // namespace

namespace snaptray::region {

CaptureChromeDirtyReason captureChromeDirtyReasonForToolPreview(
    bool pencilToolActive,
    const QRegion& previewDirtyRegion)
{
    return pencilToolActive && !previewDirtyRegion.isEmpty()
        ? CaptureChromeDirtyReason::PencilPreview
        : CaptureChromeDirtyReason::Scene;
}

void CaptureChromePendingDirtyRegions::add(const QRegion& region,
                                           CaptureChromeDirtyReason reason)
{
    if (reason == CaptureChromeDirtyReason::PencilPreview) {
        pencilPreview += region;
    } else {
        scene += region;
    }
}

void CaptureChromePendingDirtyRegions::clear()
{
    scene = QRegion();
    pencilPreview = QRegion();
}

CaptureChromePendingDirtyRegions CaptureChromePendingDirtyRegions::take()
{
    CaptureChromePendingDirtyRegions pending = *this;
    clear();
    return pending;
}

QRegion planCaptureChromeDirtyRegion(const CaptureChromeDirtyRegionPlanInput& input)
{
    const QRegion fullRegion(input.windowRect);
    if (input.forceFullRepaint) {
        return fullRegion;
    }

    if (input.conservativeStateCompositing) {
        // Windows transparent capture chrome needs a full clear for any scene or
        // render-state change. A Pencil preview is the only safe partial path:
        // its dirty bounds contain both the old and new mutable tail.
        if (input.renderStateChanged ||
            !input.sceneDirtyRegion.isEmpty() ||
            !input.stateDirtyRegion.isEmpty()) {
            return fullRegion;
        }

        return input.previewDirtyRegion.intersected(fullRegion);
    }

    QRegion dirtyRegion = input.sceneDirtyRegion;
    dirtyRegion += input.previewDirtyRegion;
    dirtyRegion += input.stateDirtyRegion;
    return dirtyRegion.intersected(fullRegion);
}

QRegion planCaptureChromeDirtyRegionForCurrentPlatform(
    CaptureChromeDirtyRegionPlanInput input)
{
#ifdef Q_OS_WIN
    input.conservativeStateCompositing = true;
#else
    input.conservativeStateCompositing = false;
#endif
    return planCaptureChromeDirtyRegion(input);
}

} // namespace snaptray::region

CaptureChromeWindow::CaptureChromeWindow(QWidget* parent)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                  Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
    , m_regionPainter(this)
    , m_useNativeLayeredBackend(preferNativeLayeredBackend())
{
    Q_UNUSED(parent);

    if (m_useNativeLayeredBackend) {
        setAttribute(Qt::WA_NativeWindow);
    }

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    m_regionPainter.setParentWidget(this);
    hide();
}

void CaptureChromeWindow::setSelectionManager(SelectionStateManager* manager)
{
    m_selectionManager = manager;
    m_regionPainter.setSelectionManager(manager);
}

void CaptureChromeWindow::setAnnotationLayer(AnnotationLayer* layer)
{
    m_annotationLayer = layer;
    m_regionPainter.setAnnotationLayer(layer);
}

void CaptureChromeWindow::setToolManager(ToolManager* manager)
{
    m_toolManager = manager;
    m_regionPainter.setToolManager(manager);
}

void CaptureChromeWindow::setShortcutHintsOverlay(CaptureShortcutHintsOverlay* overlay)
{
    m_shortcutHintsOverlay = overlay;
}

void CaptureChromeWindow::markDirtyRegion(
    const QRegion& region,
    snaptray::region::CaptureChromeDirtyReason reason)
{
    if (region.isEmpty()) {
        return;
    }

    m_pendingDirtyRegions.add(region, reason);
}

void CaptureChromeWindow::syncToHost(QWidget* host,
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
                                     bool shouldShow)
{
    snaptray::region::CapturePerfScope perfScope("CaptureChromeWindow.syncToHost");

    const QRect previousSelectionRect = m_selectionRect;
    const bool previousHasActiveSelection = m_hasActiveSelection;
    const QRect previousHighlightedWindowRect = m_highlightedWindowRect;
    QWidget* const previousHost = m_host;
    const qreal previousDevicePixelRatio = m_devicePixelRatio;
    const int previousCornerRadius = m_cornerRadius;
    const int previousCurrentTool = m_currentTool;
    const bool previousShowShortcutHints = m_showShortcutHints;
    const LoadingSpinnerRenderer* previousLoadingSpinner = m_loadingSpinner;
    const bool previousShowBusySpinner = m_showBusySpinner;
    const QPoint previousBusySpinnerCenter = m_busySpinnerCenter;
    const QRect previousGeometry = geometry();
    const bool wasVisible = isVisible();

    const qreal normalizedDevicePixelRatio =
        devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;

    m_host = host;
    m_selectionRect = selectionRect.normalized();
    m_hasActiveSelection = hasActiveSelection;
    m_highlightedWindowRect = highlightedWindowRect;
    m_devicePixelRatio = normalizedDevicePixelRatio;
    m_cornerRadius = cornerRadius;
    m_currentTool = currentTool;
    m_showShortcutHints = showShortcutHints;
    m_loadingSpinner = loadingSpinner;
    m_showBusySpinner = showBusySpinner;
    m_busySpinnerCenter = busySpinnerCenter;

    if (!m_host || !m_host->isVisible() || !shouldShow || !m_selectionManager) {
        hideOverlay();
        return;
    }

    const QRect globalRect(m_host->mapToGlobal(QPoint(0, 0)), m_host->size());
    const bool geometryChanged = previousGeometry != globalRect;
    if (geometry() != globalRect) {
        setGeometry(globalRect);
    }

    bool becameVisible = false;
    if (!isVisible()) {
        show();
        becameVisible = true;
    }

    m_regionPainter.setParentWidget(this);
    m_regionPainter.setDevicePixelRatio(m_devicePixelRatio);
    m_regionPainter.setCornerRadius(m_cornerRadius);

    const snaptray::region::CaptureChromePendingDirtyRegions pendingDirtyRegions =
        m_pendingDirtyRegions.take();

    const bool selectionVisualStateChanged =
        previousSelectionRect != m_selectionRect ||
        previousHasActiveSelection != m_hasActiveSelection ||
        previousHighlightedWindowRect != m_highlightedWindowRect ||
        previousCornerRadius != m_cornerRadius;
    const bool spinnerStateChanged =
        previousLoadingSpinner != m_loadingSpinner ||
        previousShowBusySpinner != m_showBusySpinner ||
        previousBusySpinnerCenter != m_busySpinnerCenter;
    const bool shortcutHintsChanged =
        previousShowShortcutHints != m_showShortcutHints;
    const bool broadRenderStateChanged =
        previousHost != m_host ||
        !qFuzzyCompare(previousDevicePixelRatio, m_devicePixelRatio) ||
        previousCurrentTool != m_currentTool;
    const bool renderStateChanged =
        selectionVisualStateChanged || spinnerStateChanged ||
        shortcutHintsChanged || broadRenderStateChanged;

    QRegion stateDirtyRegion;
    if (selectionVisualStateChanged) {
        const QRect oldActiveRect = activeVisualRect(
            m_regionPainter,
            previousSelectionRect,
            previousHasActiveSelection,
            previousHighlightedWindowRect);
        const QRect newActiveRect = activeVisualRect(
            m_regionPainter,
            m_selectionRect,
            m_hasActiveSelection,
            m_highlightedWindowRect);
        if (oldActiveRect.isValid() && !oldActiveRect.isEmpty()) {
            stateDirtyRegion += oldActiveRect;
        }
        if (newActiveRect.isValid() && !newActiveRect.isEmpty()) {
            stateDirtyRegion += newActiveRect;
        }
    }

    if (spinnerStateChanged) {
        const QRect oldSpinnerRect = spinnerRect(
            previousLoadingSpinner,
            previousShowBusySpinner,
            previousBusySpinnerCenter);
        const QRect newSpinnerRect = spinnerRect(
            m_loadingSpinner,
            m_showBusySpinner,
            m_busySpinnerCenter);
        if (oldSpinnerRect.isValid() && !oldSpinnerRect.isEmpty()) {
            stateDirtyRegion += oldSpinnerRect;
        }
        if (newSpinnerRect.isValid() && !newSpinnerRect.isEmpty()) {
            stateDirtyRegion += newSpinnerRect;
        }
    }

    if (shortcutHintsChanged) {
        const QRect oldShortcutHintsRect = shortcutHintsRect(
            m_shortcutHintsOverlay,
            previousShowShortcutHints,
            size());
        const QRect newShortcutHintsRect = shortcutHintsRect(
            m_shortcutHintsOverlay,
            m_showShortcutHints,
            size());
        if (oldShortcutHintsRect.isValid() && !oldShortcutHintsRect.isEmpty()) {
            stateDirtyRegion += oldShortcutHintsRect;
        }
        if (newShortcutHintsRect.isValid() && !newShortcutHintsRect.isEmpty()) {
            stateDirtyRegion += newShortcutHintsRect;
        }
    }

    snaptray::region::CaptureChromeDirtyRegionPlanInput planInput;
    planInput.windowRect = rect();
    planInput.sceneDirtyRegion = pendingDirtyRegions.scene;
    planInput.previewDirtyRegion = pendingDirtyRegions.pencilPreview;
    planInput.stateDirtyRegion = stateDirtyRegion;
    planInput.forceFullRepaint =
        geometryChanged || becameVisible || !wasVisible ||
        shortcutHintsChanged || broadRenderStateChanged;
    planInput.renderStateChanged = renderStateChanged;
    const QRegion dirtyRegion =
        snaptray::region::planCaptureChromeDirtyRegionForCurrentPlatform(planInput);

    if (!dirtyRegion.isEmpty()) {
        update(dirtyRegion);
    }
}

void CaptureChromeWindow::hideOverlay()
{
    m_lastDimensionInfoRect = QRect();
    m_pendingDirtyRegions.clear();
    hide();
}

void CaptureChromeWindow::paintEvent(QPaintEvent* event)
{
    const QRegion dirtyRegion = event ? event->region() : QRegion(rect());
    QPainter painter(this);
    painter.setClipRegion(dirtyRegion);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(dirtyRegion.boundingRect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    m_regionPainter.setParentWidget(this);
    m_regionPainter.setHighlightedWindowRect(m_highlightedWindowRect);
    m_regionPainter.setCornerRadius(m_cornerRadius);
    m_regionPainter.setShowSubToolbar(false);
    m_regionPainter.setCurrentTool(m_currentTool);
    m_regionPainter.setDevicePixelRatio(m_devicePixelRatio);
    m_regionPainter.setMultiRegionMode(false);
    m_regionPainter.setSelectionPreviewActive(false);
    m_regionPainter.setCaptureChromeActive(false);
    const QRect annotationViewport =
        m_hasActiveSelection
        ? m_selectionRect.adjusted(-64, -64, 64, 64).intersected(rect())
        : QRect();
    m_regionPainter.setAnnotationViewport(annotationViewport);
    m_regionPainter.setReplacePreview(-1, QRect());
    m_regionPainter.paint(painter, QPixmap(), dirtyRegion);
    m_lastDimensionInfoRect = m_regionPainter.lastDimensionInfoRect();

    if (m_showBusySpinner && m_loadingSpinner) {
        m_loadingSpinner->draw(painter, m_busySpinnerCenter);
    }

    if (m_showShortcutHints && m_shortcutHintsOverlay) {
        m_shortcutHintsOverlay->draw(painter, size());
    }

    emit framePainted();
}

void CaptureChromeWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    setWindowClickThrough(this, true);
    setWindowFloatingWithoutFocus(this);
    setWindowExcludedFromCapture(this, true);
    setWindowVisibleOnAllWorkspaces(this, true);
    raiseWindowAboveOverlays(this);
    raise();
    snaptray::region::CapturePerfRecorder::recordValue(
        "CaptureChromeWindow.backend",
        m_useNativeLayeredBackend ? QStringLiteral("native-layered-gate")
                                  : QStringLiteral("qt-top-level"));
}
