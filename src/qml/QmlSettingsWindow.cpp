#include "qml/QmlSettingsWindow.h"
#include "qml/QmlOverlayManager.h"
#include "qml/SettingsBackend.h"
#include "qml/QmlToast.h"
#include "PlatformFeatures.h"

#include <QCloseEvent>
#include <QEvent>
#include <QQmlContext>
#include <QQuickView>

namespace SnapTray {

QmlSettingsWindow::QmlSettingsWindow(QObject* parent)
    : QObject(parent)
{
}

QmlSettingsWindow::~QmlSettingsWindow()
{
    delete m_view.data();
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

    m_view->setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsWindow.qml")));
    m_view->setTitle(tr("Settings"));
    m_view->installEventFilter(this);

    connect(m_backend, &SettingsBackend::ocrLanguagesChanged,
            this, &QmlSettingsWindow::ocrLanguagesChanged);
    connect(m_backend, &SettingsBackend::mcpEnabledChanged,
            this, &QmlSettingsWindow::mcpEnabledChanged);
    connect(m_backend, &SettingsBackend::settingsSaved,
            this, [this]() {
        QmlToast::screenToast().showToast(
            QmlToast::Level::Success, tr("Settings saved"), QString(), 2000);
        if (!m_view)
            return;
        m_allowDirectClose = true;
        m_view->close();
        m_allowDirectClose = false;
    });

    // Route backend-driven close through a guard so user-initiated title-bar
    // close can still be transformed into cancel() without recursion.
    connect(m_backend, &SettingsBackend::settingsCancelled,
            this, [this]() {
        if (!m_view)
            return;
        m_allowDirectClose = true;
        m_view->close();
        m_allowDirectClose = false;
    });

#ifdef Q_OS_MAC
    // Revert to accessory (LSUIElement) mode when the window is dismissed
    // for any reason (Save, Cancel, or the title-bar close button).
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

#ifdef Q_OS_MAC
    PlatformFeatures::activateApp();
#endif
}

void QmlSettingsWindow::raise()
{
    if (m_view) {
        m_view->raise();
        m_view->requestActivate();
#ifdef Q_OS_MAC
        PlatformFeatures::activateApp();
#endif
    }
}

void QmlSettingsWindow::activateWindow()
{
    if (m_view) {
        m_view->requestActivate();
#ifdef Q_OS_MAC
        PlatformFeatures::activateApp();
#endif
    }
}

bool QmlSettingsWindow::isVisible() const
{
    return m_view && m_view->isVisible();
}

bool QmlSettingsWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event && event->type() == QEvent::Close) {
        auto* closeEvent = static_cast<QCloseEvent*>(event);
        if (!m_allowDirectClose && m_backend) {
            closeEvent->ignore();
            m_backend->cancel();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace SnapTray
