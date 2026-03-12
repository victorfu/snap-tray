#include "qml/QmlSettingsWindow.h"
#include "qml/QmlOverlayManager.h"
#include "qml/QmlDialog.h"
#include "qml/SettingsBackend.h"
#include "qml/QmlToast.h"
#include "qml/UpdateDialogViewModel.h"
#include "PlatformFeatures.h"

#include <QQmlContext>
#include <QQuickView>

namespace SnapTray {

namespace {
void showUpdateDialog(QObject* parent, UpdateDialogViewModel* viewModel,
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

    dialog->showAt();
}
}

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
    connect(m_backend, &SettingsBackend::updateAvailable,
            this, [this](const QString& version, const QString& notes,
                         const QString& releaseUrl) {
        ReleaseInfo release;
        release.version = version;
        release.releaseNotes = notes;
        release.htmlUrl = releaseUrl;
        showUpdateDialog(this,
                         UpdateDialogViewModel::createForRelease(release, this),
                         m_backend);
    });
    connect(m_backend, &SettingsBackend::noUpdateAvailable,
            this, [this]() {
        showUpdateDialog(this,
                         UpdateDialogViewModel::createUpToDate(this),
                         m_backend);
    });
    connect(m_backend, &SettingsBackend::updateCheckFailed,
            this, [this](const QString& error) {
        showUpdateDialog(this,
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
