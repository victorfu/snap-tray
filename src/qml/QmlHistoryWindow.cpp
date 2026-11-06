#include "qml/QmlHistoryWindow.h"

#include "cursor/CursorSurfaceSupport.h"
#include "qml/HistoryBackend.h"
#include "qml/HistoryModel.h"
#include "qml/LibraryModel.h"
#include "qml/RecordingLibraryViewModel.h"
#include "qml/QmlOverlayManager.h"
#include "qml/QmlToast.h"

#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QUrl>

namespace SnapTray {

QmlHistoryWindow::QmlHistoryWindow(PinWindowManager* pinWindowManager,
                                   std::function<bool(const QString&)> startReplay,
                                   QObject* parent)
    : QObject(parent)
    , m_pinWindowManager(pinWindowManager)
    , m_startReplay(std::move(startReplay))
{
}

QmlHistoryWindow::~QmlHistoryWindow()
{
    CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
    delete m_view;
}

void QmlHistoryWindow::show()
{
    ensureView();
    if (m_hasShownOnce) {
        // A delete left pending when the window closed is not asked again.
        if (QObject* root = m_view->rootObject())
            QMetaObject::invokeMethod(root, "cancelDelete");
        m_backend->refresh();
        m_recordings->refresh();
    } else {
        m_hasShownOnce = true;
    }
    m_view->show();
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view);
    m_view->raise();
    m_view->requestActivate();
    syncCursorSurface();
}

void QmlHistoryWindow::showNormal()
{
    ensureView();
    m_view->setWindowState(Qt::WindowNoState);
    show();
}

void QmlHistoryWindow::raise()
{
    if (m_view) {
        m_view->raise();
        syncCursorSurface();
    }
}

void QmlHistoryWindow::activateWindow()
{
    if (m_view) {
        m_view->requestActivate();
        syncCursorSurface();
    }
}

bool QmlHistoryWindow::isVisible() const
{
    return m_view && m_view->isVisible();
}

bool QmlHistoryWindow::isMinimized() const
{
    return m_view && (m_view->windowState() & Qt::WindowMinimized);
}

void QmlHistoryWindow::showSection(int source)
{
    ensureView();
    if (QObject* root = m_view->rootObject())
        QMetaObject::invokeMethod(root, "showSection", Q_ARG(QVariant, source));
}

void QmlHistoryWindow::setOpenProjectDir(const QString& dir)
{
    m_openProjectDir = dir;
    if (!m_recordings) return;
    m_recordings->setOpenProjectDir(dir);
    m_recordings->refresh();   // a preview save may have kept a new project
}

void QmlHistoryWindow::ensureView()
{
    if (m_view) {
        return;
    }

    m_model = new HistoryModel(this);
    m_view = QmlOverlayManager::instance().createUtilityWindow();
    m_backend = new HistoryBackend(
        m_model,
        m_pinWindowManager,
        m_startReplay,
        m_view,
        this);
    // Screenshots and editable recording projects, one window (#226).
    m_recordings = new RecordingLibraryViewModel(this);
    m_recordings->setOpenProjectDir(m_openProjectDir);
    m_library = new LibraryModel(m_model, m_recordings, this);
    connect(m_recordings, &RecordingLibraryViewModel::openRequested, this,
            &QmlHistoryWindow::openProjectRequested);
    m_view->setTitle(tr("Library"));

    // Below 820 px the sidebar folds into a row of section buttons.
    const QSize preferredMinimumSize(640, 480);
    const QSize preferredInitialSize(1240, 820);
    QSize minimumSize = preferredMinimumSize;
    QSize initialSize = preferredInitialSize;

    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QSize availableSize = screen->availableGeometry().size();
        const QSize paddedAvailableSize(qMax(availableSize.width() - 48, 1),
                                        qMax(availableSize.height() - 64, 1));
        minimumSize = preferredMinimumSize.boundedTo(paddedAvailableSize);
        initialSize = preferredInitialSize.boundedTo(paddedAvailableSize).expandedTo(minimumSize);
    }

    m_view->setMinimumSize(minimumSize);
    m_view->resize(initialSize);
    m_view->rootContext()->setContextProperty(QStringLiteral("historyModel"), m_model);
    m_view->rootContext()->setContextProperty(QStringLiteral("historyBackend"), m_backend);
    m_view->rootContext()->setContextProperty(QStringLiteral("libraryModel"), m_library);
    m_view->rootContext()->setContextProperty(QStringLiteral("recordingLibrary"), m_recordings);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/panels/HistoryWindow.qml")));
    m_view->installEventFilter(this);
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("QmlHistoryWindow"));
    m_cursorOwnerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("QmlHistoryWindow"));

    connect(m_backend, &HistoryBackend::closeRequested, this, [this]() {
        if (m_view) {
            m_view->close();
        }
    });
    connect(m_backend, &HistoryBackend::toastRequested, this,
            [](int level, const QString& title, const QString& message) {
                QmlToast::screenToast().showToast(
                    static_cast<QmlToast::Level>(level),
                    title,
                    message);
            });
}

bool QmlHistoryWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_view || !m_view) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            // Esc first cancels a pending delete; the Library handles it.
            QObject* root = m_view->rootObject();
            if (root && root->property("confirming").toBool())
                return QObject::eventFilter(watched, event);
            m_view->close();
            return true;
        }
    }

    if (event->type() == QEvent::Hide || event->type() == QEvent::Close) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
    } else if (CursorSurfaceSupport::isPointerRefreshEvent(event->type())) {
        syncCursorSurface();
    }

    return QObject::eventFilter(watched, event);
}

void QmlHistoryWindow::syncCursorSurface()
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

} // namespace SnapTray
