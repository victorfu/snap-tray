#include "qml/QmlPinHistoryWindow.h"

#include "qml/PinHistoryBackend.h"
#include "qml/PinHistoryModel.h"
#include "qml/QmlOverlayManager.h"
#include "qml/QmlToast.h"

#include <QEvent>
#include <QDebug>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQuickView>
#include <QUrl>

namespace SnapTray {

QmlPinHistoryWindow::QmlPinHistoryWindow(PinWindowManager* pinWindowManager, QObject* parent)
    : QObject(parent)
    , m_pinWindowManager(pinWindowManager)
{
}

QmlPinHistoryWindow::~QmlPinHistoryWindow()
{
    delete m_view;
}

void QmlPinHistoryWindow::show()
{
    ensureView();
    if (m_hasShownOnce) {
        m_backend->refresh();
    } else {
        m_hasShownOnce = true;
    }
    m_view->show();
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view);
    m_view->raise();
    m_view->requestActivate();
}

void QmlPinHistoryWindow::showNormal()
{
    ensureView();
    m_view->setWindowState(Qt::WindowNoState);
    show();
}

void QmlPinHistoryWindow::raise()
{
    if (m_view) {
        m_view->raise();
    }
}

void QmlPinHistoryWindow::activateWindow()
{
    if (m_view) {
        m_view->requestActivate();
    }
}

bool QmlPinHistoryWindow::isVisible() const
{
    return m_view && m_view->isVisible();
}

bool QmlPinHistoryWindow::isMinimized() const
{
    return m_view && (m_view->windowState() & Qt::WindowMinimized);
}

void QmlPinHistoryWindow::ensureView()
{
    if (m_view) {
        return;
    }

    m_model = new PinHistoryModel(this);
    m_view = QmlOverlayManager::instance().createUtilityWindow();
    m_backend = new PinHistoryBackend(m_model, m_pinWindowManager, m_view, this);
    m_view->setTitle(tr("Pin History"));
    m_view->setMinimumSize(QSize(480, 320));
    m_view->resize(760, 520);
    m_view->rootContext()->setContextProperty(QStringLiteral("pinHistoryModel"), m_model);
    m_view->rootContext()->setContextProperty(QStringLiteral("pinHistoryBackend"), m_backend);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/panels/PinHistoryWindow.qml")));
    m_view->installEventFilter(this);

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors()) {
            qWarning() << "QmlPinHistoryWindow QML error:" << error.toString();
        }
    }

    connect(m_backend, &PinHistoryBackend::closeRequested, this, [this]() {
        if (m_view) {
            m_view->close();
        }
    });
    connect(m_backend, &PinHistoryBackend::toastRequested, this,
            [](int level, const QString& title, const QString& message) {
                QmlToast::screenToast().showToast(
                    static_cast<QmlToast::Level>(level),
                    title,
                    message);
            });
}

bool QmlPinHistoryWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_view || !m_view) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            m_view->close();
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

} // namespace SnapTray
