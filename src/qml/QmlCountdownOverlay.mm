#include "qml/QmlCountdownOverlay.h"
#include "cursor/CursorSurfaceSupport.h"
#include "qml/QmlOverlayManager.h"

#include <QEvent>
#include <QQuickView>
#include <QQuickItem>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

QmlCountdownOverlay::QmlCountdownOverlay(QObject* parent)
    : QObject(parent)
{
}

QmlCountdownOverlay::~QmlCountdownOverlay()
{
    CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
    if (m_view) {
        m_view->close();
        m_rootItem = nullptr;
        // Use deleteLater() because this object may be destroyed from a QML
        // signal handler (e.g. countdown finished → RecordingManager deletes us).
        // Synchronous delete would crash: "Object destroyed while signal handler in progress".
        m_view->deleteLater();
        m_view = nullptr;
    }
}

void QmlCountdownOverlay::ensureView()
{
    if (m_view)
        return;

    m_view = SnapTray::QmlOverlayManager::instance().createScreenOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/components/Countdown.qml")));
    m_view->installEventFilter(this);
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("QmlCountdownOverlay"));
    m_cursorOwnerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("QmlCountdownOverlay"));

    // Countdown root item should fill the overlay geometry set from C++.
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);

    // Do NOT set WindowTransparentForInput — we need keyboard focus for Escape

    m_rootItem = m_view->rootObject();

    if (m_rootItem) {
        connect(m_rootItem, SIGNAL(finished()), this, SIGNAL(countdownFinished()));
        connect(m_rootItem, SIGNAL(cancelled()), this, SIGNAL(countdownCancelled()));
    } else {
        qWarning() << "QmlCountdownOverlay: rootObject is null after loading Countdown.qml";
    }
}

void QmlCountdownOverlay::applyPlatformWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView* view = reinterpret_cast<NSView*>(m_view->winId());
    if (!view)
        return;

    NSWindow* window = [view window];
    if (!window)
        return;

    // Raise above menu bar
    [window setLevel:kCGScreenSaverWindowLevel];

    // Keep visible when app loses focus
    [window setHidesOnDeactivate:NO];

    // Do NOT call setIgnoresMouseEvents — must accept keyboard input
#endif
}

void QmlCountdownOverlay::setRegion(const QRect& region)
{
    m_region = region;
    ensureView();

    m_view->setGeometry(region);
}

void QmlCountdownOverlay::setCountdownSeconds(int seconds)
{
    m_countdownSeconds = qBound(1, seconds, 10);
    ensureView();

    if (m_rootItem)
        m_rootItem->setProperty("countdownSeconds", m_countdownSeconds);
}

void QmlCountdownOverlay::start()
{
    ensureView();

    if (m_rootItem)
        m_rootItem->setProperty("countdownSeconds", m_countdownSeconds);

    if (!m_rootItem) {
        qWarning() << "QmlCountdownOverlay: cannot show overlay — rootItem is null";
        return;
    }

    m_view->show();
    applyPlatformWindowFlags();

    // Request keyboard focus for Escape handling
    m_view->raise();
    m_view->requestActivate();

    m_running = true;
    syncCursorSurface();

    if (m_rootItem)
        QMetaObject::invokeMethod(m_rootItem, "start");
}

void QmlCountdownOverlay::cancel()
{
    if (!m_running)
        return;

    m_running = false;

    if (m_rootItem)
        QMetaObject::invokeMethod(m_rootItem, "cancel");
}

void QmlCountdownOverlay::close()
{
    m_running = false;

    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->close();
        m_rootItem = nullptr;
        m_view->deleteLater();
        m_view = nullptr;
    }
}

bool QmlCountdownOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event) {
        if (event->type() == QEvent::Hide || event->type() == QEvent::Close) {
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        } else if (CursorSurfaceSupport::isPointerRefreshEvent(event->type())) {
            syncCursorSurface();
        }
    }

    return QObject::eventFilter(watched, event);
}

void QmlCountdownOverlay::syncCursorSurface()
{
    if (!m_view || m_cursorSurfaceId.isEmpty() || m_cursorOwnerId.isEmpty()) {
        return;
    }

    if (!m_view->isVisible()) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        return;
    }

    CursorSurfaceSupport::syncWindowSurface(
        m_view, m_cursorSurfaceId, m_cursorOwnerId, CursorRequestSource::SurfaceDefault);
}
