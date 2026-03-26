#include "region/StaticCaptureBackgroundWindow.h"

#include "region/CapturePerfRecorder.h"
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

StaticCaptureBackgroundWindow::StaticCaptureBackgroundWindow(QWidget* parent)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                  Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
    , m_useNativeLayeredBackend(preferNativeLayeredBackend())
{
    Q_UNUSED(parent);

    if (m_useNativeLayeredBackend) {
        setAttribute(Qt::WA_NativeWindow);
    }

    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void StaticCaptureBackgroundWindow::syncToHost(QWidget* host,
                                               const QPixmap& backgroundPixmap,
                                               bool shouldShow)
{
    snaptray::region::CapturePerfScope perfScope("StaticCaptureBackgroundWindow.syncToHost");

    const qint64 previousCacheKey = m_backgroundPixmap.cacheKey();
    const qreal previousDpr = m_backgroundPixmap.devicePixelRatio();
    const QRect previousGeometry = geometry();
    const bool wasVisible = isVisible();

    m_host = host;
    m_backgroundPixmap = backgroundPixmap;

    if (!m_host || !m_host->isVisible() || !shouldShow || m_backgroundPixmap.isNull()) {
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

    if (m_host && (becameVisible || geometryChanged)) {
        m_host->raise();
    }

    const bool backgroundChanged =
        previousCacheKey != m_backgroundPixmap.cacheKey() ||
        !qFuzzyCompare(previousDpr, m_backgroundPixmap.devicePixelRatio());
    if (backgroundChanged || geometryChanged || becameVisible || !wasVisible) {
        update();
    }
}

void StaticCaptureBackgroundWindow::hideOverlay()
{
    hide();
}

void StaticCaptureBackgroundWindow::paintEvent(QPaintEvent* event)
{
    if (m_backgroundPixmap.isNull()) {
        return;
    }

    const QRegion dirtyRegion = event ? event->region() : QRegion(rect());
    QPainter painter(this);
    painter.setClipRegion(dirtyRegion);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawPixmap(QRect(QPoint(0, 0), size()), m_backgroundPixmap);
}

void StaticCaptureBackgroundWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    setWindowClickThrough(this, true);
    setWindowFloatingWithoutFocus(this);
    setWindowExcludedFromCapture(this, true);
    setWindowVisibleOnAllWorkspaces(this, true);
    snaptray::region::CapturePerfRecorder::recordValue(
        "StaticCaptureBackgroundWindow.backend",
        m_useNativeLayeredBackend ? QStringLiteral("native-layered-gate")
                                  : QStringLiteral("qt-top-level"));
}
