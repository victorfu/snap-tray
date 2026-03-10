#include "qml/QmlDialog.h"
#include "qml/QmlOverlayManager.h"

#include <QQuickView>
#include <QScreen>
#include <QGuiApplication>
#include <QVariant>

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

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "QmlDialog QML error:" << error.toString();
    }

    connect(m_view, &QQuickView::closing, this, [this]() {
        emit closed();
    });
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

    m_view->show();

    QmlOverlayManager::enableNativeShadow(m_view);
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view);

#ifdef Q_OS_MACOS
    // Raise above overlays
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

    m_view->raise();
    m_view->requestActivate();
}

void QmlDialog::close()
{
    if (m_view) {
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

} // namespace SnapTray
