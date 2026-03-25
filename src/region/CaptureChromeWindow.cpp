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

bool preferNativeLayeredBackend()
{
#if defined(Q_OS_WIN) && !defined(QT_NO_DEBUG_OUTPUT)
    return qEnvironmentVariableIntValue("SNAPTRAY_CAPTURE_NATIVE_WINDOWS") > 0;
#else
    return false;
#endif
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
        return;
    }

    const QRect globalRect(m_host->mapToGlobal(QPoint(0, 0)), m_host->size());
    if (geometry() != globalRect) {
        setGeometry(globalRect);
    }

    if (!isVisible()) {
        show();
    }

    update();
}

void CaptureChromeWindow::hideOverlay()
{
    m_lastDimensionInfoRect = QRect();
    hide();
}

void CaptureChromeWindow::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
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
    m_regionPainter.setReplacePreview(-1, QRect());
    m_regionPainter.paint(painter, QPixmap(), event ? event->region() : QRegion(rect()));
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
