#include "qml/QmlBeautifyPanel.h"

#include "qml/BeautifyPanelBackend.h"
#include "qml/QmlOverlayManager.h"

#include <QEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QUrl>

namespace SnapTray {

QmlBeautifyPanel::QmlBeautifyPanel(QObject* parent)
    : QObject(parent)
{
}

QmlBeautifyPanel::~QmlBeautifyPanel()
{
    close();
}

void QmlBeautifyPanel::setSourcePixmap(const QPixmap& pixmap)
{
    ensureView();
    m_backend->setSourcePixmap(pixmap);
}

void QmlBeautifyPanel::setSettings(const BeautifySettings& settings)
{
    ensureView();
    m_backend->setSettings(settings);
}

void QmlBeautifyPanel::showNear(const QRect& anchorRect)
{
    ensureView();
    positionNear(anchorRect);
    m_view->show();
    QmlOverlayManager::configureInteractiveOverlayWindow(m_view);
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view);
    QmlOverlayManager::enableNativeShadow(m_view);
    m_view->raise();
    m_view->requestActivate();
}

void QmlBeautifyPanel::hide()
{
    if (m_view) {
        m_view->hide();
    }
}

void QmlBeautifyPanel::close()
{
    if (!m_view) {
        return;
    }

    m_view->removeEventFilter(this);
    m_view->close();
    delete m_view;
    m_view = nullptr;
    m_rootItem = nullptr;
}

bool QmlBeautifyPanel::isVisible() const
{
    return m_view && m_view->isVisible();
}

QRect QmlBeautifyPanel::geometry() const
{
    if (!m_view) {
        return QRect();
    }
    return QRect(m_view->position(), m_view->size());
}

void QmlBeautifyPanel::onDragStarted()
{
    if (!m_view) {
        return;
    }

    m_dragStartWindowPos = m_view->position();
    m_dragStartCursorPos = QCursor::pos();
    m_isDragging = true;
}

void QmlBeautifyPanel::onDragMoved(double deltaX, double deltaY)
{
    Q_UNUSED(deltaX);
    Q_UNUSED(deltaY);

    if (!m_view || !m_isDragging) {
        return;
    }

    QPoint newPos = m_dragStartWindowPos + (QCursor::pos() - m_dragStartCursorPos);
    QScreen* screen = QGuiApplication::screenAt(
        newPos + QPoint(m_view->width() / 2, m_view->height() / 2));
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen) {
        const QRect screenGeom = screen->availableGeometry();
        newPos.setX(qBound(screenGeom.left(), newPos.x(), screenGeom.right() - m_view->width()));
        newPos.setY(qBound(screenGeom.top(), newPos.y(), screenGeom.bottom() - m_view->height()));
    }

    m_view->setPosition(newPos);
}

void QmlBeautifyPanel::onDragFinished()
{
    m_isDragging = false;
}

void QmlBeautifyPanel::ensureView()
{
    if (m_view) {
        return;
    }

    m_backend = new BeautifyPanelBackend(this);
    m_view = QmlOverlayManager::instance().createToolPanelWindow();
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, false);
    m_view->rootContext()->setContextProperty(QStringLiteral("beautifyBackend"), m_backend);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/panels/BeautifyPanel.qml")));
    m_view->installEventFilter(this);

    m_rootItem = m_view->rootObject();
    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors()) {
            qWarning() << "QmlBeautifyPanel QML error:" << error.toString();
        }
    }

    connect(m_backend, &BeautifyPanelBackend::copyRequested,
            this, &QmlBeautifyPanel::copyRequested);
    connect(m_backend, &BeautifyPanelBackend::saveRequested,
            this, &QmlBeautifyPanel::saveRequested);
    connect(m_backend, &BeautifyPanelBackend::closeRequested,
            this, [this]() {
                hide();
                emit closeRequested();
            });

    if (m_rootItem) {
        connect(m_rootItem, SIGNAL(dragStarted()), this, SLOT(onDragStarted()));
        connect(m_rootItem, SIGNAL(dragMoved(double,double)), this, SLOT(onDragMoved(double,double)));
        connect(m_rootItem, SIGNAL(dragFinished()), this, SLOT(onDragFinished()));
    }
}

void QmlBeautifyPanel::positionNear(const QRect& anchorRect)
{
    if (!m_view) {
        return;
    }

    QScreen* screen = QGuiApplication::screenAt(anchorRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const QRect screenGeom = screen ? screen->availableGeometry() : QRect();
    const QSize panelSize = m_view->size();
    const int gap = 8;

    int x = anchorRect.right() + gap;
    int y = anchorRect.top();
    if (screen && x + panelSize.width() > screenGeom.right() - 10) {
        x = anchorRect.left() - panelSize.width() - gap;
    }

    if (screen) {
        x = qBound(screenGeom.left() + 10, x, screenGeom.right() - panelSize.width() - 10);
        y = qBound(screenGeom.top() + 10, y, screenGeom.bottom() - panelSize.height() - 10);
    }

    m_view->setPosition(x, y);
}

bool QmlBeautifyPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_view || !m_view) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress && m_view->isVisible()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hide();
            emit closeRequested();
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

} // namespace SnapTray
