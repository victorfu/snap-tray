#include "qml/QmlCountdownOverlay.h"
#include "qml/QmlOverlayManager.h"

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
    if (m_view) {
        m_view->close();
        m_rootItem = nullptr;
        delete m_view;
        m_view = nullptr;
    }
}

void QmlCountdownOverlay::ensureView()
{
    if (m_view)
        return;

    m_view = SnapTray::QmlOverlayManager::instance().createScreenOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/components/Countdown.qml")));

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
        m_view->close();
        m_rootItem = nullptr;
        delete m_view;
        m_view = nullptr;
    }
}
