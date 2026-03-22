#include "qml/QmlDialog.h"
#include "cursor/CursorSurfaceSupport.h"
#include "platform/WindowLevel.h"
#include "qml/QmlOverlayManager.h"

#include <QEvent>
#include <QQuickView>
#include <QScreen>
#include <QGuiApplication>
#include <QVariant>
#include <QWidget>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace SnapTray {

QmlDialog::QmlDialog(const QUrl& qmlSource, QObject* viewModel,
                     const QString& contextPropertyName, QObject* parent)
    : QObject(parent)
    , m_qmlSource(qmlSource)
    , m_viewModel(viewModel)
    , m_contextPropertyName(contextPropertyName)
{
    // Take ownership of the ViewModel so it is cleaned up together with this dialog.
    if (viewModel)
        viewModel->setParent(this);
}

QmlDialog::~QmlDialog()
{
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
    }
}

void QmlDialog::ensureView()
{
    if (m_view)
        return;

    if (!m_viewModel) {
        qWarning("QmlDialog::ensureView: viewModel has been destroyed");
        return;
    }

    m_view = QmlOverlayManager::instance().createScreenOverlay();

    // Use initial properties so the viewModel QML property is set before component creation.
    // This avoids null-reference errors during initial binding evaluation.
    QVariantMap initialProps;
    initialProps[m_contextPropertyName] = QVariant::fromValue(m_viewModel.data());
    m_view->setInitialProperties(initialProps);
    m_view->setSource(m_qmlSource);
    m_view->installEventFilter(this);
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("QmlDialog"));
    m_cursorOwnerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("QmlDialog"));

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "QmlDialog QML error:" << error.toString();
    }

    connect(m_view, &QQuickView::closing, this, [this]() {
        emit closed();
    });

    syncTransientParent();
}

QWidget* QmlDialog::hostWidget() const
{
    return qobject_cast<QWidget*>(parent());
}

void QmlDialog::syncTransientParent()
{
    if (!m_view) {
        return;
    }

    QWidget* hostWindow = hostWidget() ? hostWidget()->window() : nullptr;
    if (hostWindow && hostWindow->windowHandle()) {
        m_view->setTransientParent(hostWindow->windowHandle());
    } else {
        m_view->setTransientParent(nullptr);
    }

    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);

    if (m_view->isVisible()) {
        applyPlatformWindowFlags();
    }
}

void QmlDialog::applyPlatformWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_view) {
        return;
    }

    NSView* nsView = reinterpret_cast<NSView*>(m_view->winId());
    if (nsView) {
        NSWindow* nsWindow = [nsView window];
        if (nsWindow) {
            [nsWindow setOpaque:NO];
            [nsWindow setBackgroundColor:[NSColor clearColor]];
            [nsWindow setLevel:kCGScreenSaverWindowLevel + 2];
            [nsWindow makeKeyAndOrderFront:nil];
            if (m_modal) {
                [nsWindow setHidesOnDeactivate:NO];
            }
        }
    }
#endif

    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
    raiseTransientWindowAboveParent(m_view, hostWidget());
}

void QmlDialog::showAt(const QPoint& pos)
{
    ensureView();

    if (!m_view)
        return;

    if (pos.isNull()) {
        // Center on primary screen
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect screenGeometry = screen->geometry();
            const QSize viewSize = m_view->size();
            const int x = screenGeometry.center().x() - viewSize.width() / 2;
            const int y = screenGeometry.center().y() - viewSize.height() / 2;
            m_view->setPosition(x, y);
        }
    } else {
        m_view->setPosition(pos);
    }

    syncTransientParent();
    m_view->show();
    applyPlatformWindowFlags();

    QmlOverlayManager::enableNativeShadow(m_view);
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view);

    m_view->raise();
    m_view->requestActivate();
    syncCursorSurface();
}

void QmlDialog::close()
{
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->close(); // triggers QQuickView::closing -> emit closed()
    } else {
        emit closed();
    }
    deleteLater();
}

void QmlDialog::setModal(bool modal)
{
    m_modal = modal;
}

void QmlDialog::syncCursorSurface()
{
    if (!m_view || m_cursorSurfaceId.isEmpty() || m_cursorOwnerId.isEmpty()) {
        return;
    }

    if (!m_view->isVisible()) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        return;
    }

    CursorSurfaceSupport::syncWindowSurface(
        m_view, m_cursorSurfaceId, m_cursorOwnerId, CursorRequestSource::Popup);
}

bool QmlDialog::eventFilter(QObject* watched, QEvent* event)
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

} // namespace SnapTray
