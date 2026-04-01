#include "qml/QmlSettingsWindow.h"
#include "cursor/CursorSurfaceSupport.h"
#include "qml/QmlOverlayManager.h"
#include "qml/QmlDialog.h"
#include "qml/SettingsBackend.h"
#include "qml/QmlToast.h"
#include "qml/UpdateDialogViewModel.h"
#include "PlatformFeatures.h"

#include <QCursor>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScreen>

namespace SnapTray {

namespace {
void logQmlViewErrors(QQuickView* view, const QString& context)
{
    if (!view) {
        return;
    }

    const auto errors = view->errors();
    if (errors.isEmpty()) {
        qCritical().noquote() << context
                              << QStringLiteral("QML view entered error state without details.");
        return;
    }

    for (const auto& error : errors) {
        qCritical().noquote() << context << error.toString();
    }
}

void showUpdateDialog(QObject* parent, QScreen* screen, UpdateDialogViewModel* viewModel,
                      SettingsBackend* backend)
{
    auto* dialog = new QmlDialog(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/UpdateDialog.qml")),
        viewModel, QStringLiteral("viewModel"), parent);

    QObject::connect(viewModel, &UpdateDialogViewModel::retryRequested, parent,
                     [backend]() {
        if (backend) {
            backend->checkForUpdates();
        }
    });
    QObject::connect(viewModel, &UpdateDialogViewModel::closeRequested, parent,
                     [dialog]() {
        dialog->close();
    });

    dialog->showCenteredOnScreen(screen);
}
}

QmlSettingsWindow::QmlSettingsWindow(QObject* parent)
    : QObject(parent)
{
}

QmlSettingsWindow::~QmlSettingsWindow()
{
    CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
    delete m_view.data();
}

void QmlSettingsWindow::checkForUpdates()
{
    ensureView();
    if (m_backend) {
        m_backend->checkForUpdates();
    }
}

void QmlSettingsWindow::ensureView()
{
    if (m_view)
        return;

    m_backend = new SettingsBackend(this);
    m_view = QmlOverlayManager::instance().createSettingsWindow();

    // Set context property BEFORE loading QML source so that all
    // property bindings in the settings pages can resolve settingsBackend.
    m_view->rootContext()->setContextProperty(
        QStringLiteral("settingsBackend"), m_backend);
    connect(m_view, &QQuickView::statusChanged, this,
            [this](QQuickView::Status status) {
                if (status == QQuickView::Error) {
                    logQmlViewErrors(
                        m_view,
                        QStringLiteral("QmlSettingsWindow: Failed to load settings UI:"));
                }
            });

    m_view->setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsWindow.qml")));
    m_view->setTitle(tr("Settings"));
    m_view->installEventFilter(this);
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("QmlSettingsWindow"));
    m_cursorOwnerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("QmlSettingsWindow"));

    connect(m_backend, &SettingsBackend::ocrLanguagesChanged,
            this, &QmlSettingsWindow::ocrLanguagesChanged);
    connect(m_backend, &SettingsBackend::mcpEnabledChanged,
            this, &QmlSettingsWindow::mcpEnabledChanged);
    connect(m_backend, &SettingsBackend::languageChanged,
            this, [this]() {
        if (!m_view || !m_view->isVisible())
            return;

        QmlToast::screenToast().showToast(
            QmlToast::Level::Success,
            tr("Settings saved. Language change will apply after restart."),
            QString(),
            2000);
    });
    connect(m_backend, &SettingsBackend::updateCheckUnavailable,
            this, [this](const QString& reason) {
        QScreen* screen = m_view ? m_view->screen() : QGuiApplication::screenAt(QCursor::pos());
        showUpdateDialog(this,
                         screen,
                         UpdateDialogViewModel::createInfo(reason, this),
                         m_backend);
    });
    connect(m_backend, &SettingsBackend::updateCheckFailed,
            this, [this](const QString& error) {
        QScreen* screen = m_view ? m_view->screen() : QGuiApplication::screenAt(QCursor::pos());
        showUpdateDialog(this,
                         screen,
                         UpdateDialogViewModel::createError(error, this),
                         m_backend);
    });

#ifdef Q_OS_MAC
    // Revert to accessory (LSUIElement) mode when the window is dismissed.
    connect(m_view, &QWindow::visibleChanged, this, [](bool visible) {
        if (!visible)
            PlatformFeatures::setActivationPolicyAccessory();
    });
#endif
}

void QmlSettingsWindow::show()
{
    ensureView();

#ifdef Q_OS_MAC
    // Temporarily switch to regular activation policy so the window
    // survives app-deactivation (LSUIElement apps hide all windows
    // when another app comes to the foreground).
    PlatformFeatures::setActivationPolicyRegular();
#endif

    m_view->show();
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view);
    m_view->raise();
    m_view->requestActivate();
    syncCursorSurface();

#ifdef Q_OS_MAC
    PlatformFeatures::activateApp();
#endif
}

void QmlSettingsWindow::raise()
{
    if (m_view) {
        m_view->raise();
        m_view->requestActivate();
        syncCursorSurface();
#ifdef Q_OS_MAC
        PlatformFeatures::activateApp();
#endif
    }
}

void QmlSettingsWindow::activateWindow()
{
    if (m_view) {
        m_view->requestActivate();
        syncCursorSurface();
#ifdef Q_OS_MAC
        PlatformFeatures::activateApp();
#endif
    }
}

bool QmlSettingsWindow::isVisible() const
{
    return m_view && m_view->isVisible();
}

void QmlSettingsWindow::syncCursorSurface()
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

bool QmlSettingsWindow::eventFilter(QObject* watched, QEvent* event)
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
