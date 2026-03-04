#include "qml/QmlSettingsWindow.h"
#include "qml/QmlOverlayManager.h"
#include "qml/SettingsBackend.h"
#include "PlatformFeatures.h"

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

    connect(m_backend, &SettingsBackend::ocrLanguagesChanged,
            this, &QmlSettingsWindow::ocrLanguagesChanged);
    connect(m_backend, &SettingsBackend::mcpEnabledChanged,
            this, &QmlSettingsWindow::mcpEnabledChanged);
    connect(m_backend, &SettingsBackend::settingsSaved,
            m_view, &QQuickView::close);
    connect(m_backend, &SettingsBackend::settingsCancelled,
            m_view, &QQuickView::close);

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

} // namespace SnapTray
