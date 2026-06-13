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

void CaptureChromeWindow::markDirtyRegion(const QRegion& region)
{
    if (region.isEmpty()) {
        return;
    }

    m_pendingDirtyRegion += region;
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
    const bool previousShowShortcutHints = m_showShortcutHints;
    const LoadingSpinnerRenderer* previousLoadingSpinner = m_loadingSpinner;
    const bool previousShowBusySpinner = m_showBusySpinner;
    const QPoint previousBusySpinnerCenter = m_busySpinnerCenter;
    const QRect previousGeometry = geometry();
    const bool wasVisible = isVisible();

    m_host = host;
    m_selectionRect = selectionRect.normalized();
    m_hasActiveSelection = hasActiveSelection;
    m_highlightedWindowRect = highlightedWindowRect;
    m_devicePixelRatio = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    m_cornerRadius = cornerRadius;
    m_currentTool = currentTool;
    m_showShortcutHints = showShortcutHints;
    m_loadingSpinner = loadingSpinner;
    m_showBusySpinner = showBusySpinner;
    m_busySpinnerCenter = busySpinnerCenter;

    if (!m_host || !m_host->isVisible() || !shouldShow || !m_selectionManager) {
        hideOverlay();
        m_pendingDirtyRegion = QRegion();
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

    QRegion dirtyRegion = m_pendingDirtyRegion;
    m_pendingDirtyRegion = QRegion();

#ifdef Q_OS_WIN
    // The dimension label and selection chrome can leave trails on some
    // Windows compositions when partial dirty regions under-clear the previous
    // glass/chrome footprint. Favor correctness over micro-optimization here.
    dirtyRegion = QRegion(rect());
#else
    if (geometryChanged || becameVisible || !wasVisible) {
        dirtyRegion = QRegion(rect());
    } else if (previousShowShortcutHints != m_showShortcutHints) {
        // Hint-overlay show/hide transitions can leave stale detached chrome
        // when only the prior panel rect is repainted. Refresh the full window
        // so the overlay dismisses cleanly under hover and overlap transitions.
        dirtyRegion = QRegion(rect());
    } else {
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
        const QRect oldSpinnerRect = spinnerRect(
            previousLoadingSpinner,
            previousShowBusySpinner,
            previousBusySpinnerCenter);
        const QRect newSpinnerRect = spinnerRect(
            m_loadingSpinner,
            m_showBusySpinner,
            m_busySpinnerCenter);
        const QRect oldShortcutHintsRect = shortcutHintsRect(
            m_shortcutHintsOverlay,
            previousShowShortcutHints,
            size());
        const QRect newShortcutHintsRect = shortcutHintsRect(
            m_shortcutHintsOverlay,
            m_showShortcutHints,
            size());

        if (oldActiveRect.isValid() && !oldActiveRect.isEmpty()) {
            dirtyRegion += oldActiveRect;
        }
        if (newActiveRect.isValid() && !newActiveRect.isEmpty()) {
            dirtyRegion += newActiveRect;
        }
        if (oldSpinnerRect.isValid() && !oldSpinnerRect.isEmpty()) {
            dirtyRegion += oldSpinnerRect;
        }
        if (newSpinnerRect.isValid() && !newSpinnerRect.isEmpty()) {
            dirtyRegion += newSpinnerRect;
        }
        if (oldShortcutHintsRect.isValid() && !oldShortcutHintsRect.isEmpty()) {
            dirtyRegion += oldShortcutHintsRect;
        }
        if (newShortcutHintsRect.isValid() && !newShortcutHintsRect.isEmpty()) {
            dirtyRegion += newShortcutHintsRect;
        }
    }
#endif

    if (!dirtyRegion.isEmpty()) {
        update(dirtyRegion);
    }
}

void CaptureChromeWindow::hideOverlay()
{
    m_lastDimensionInfoRect = QRect();
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
