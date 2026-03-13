#include "qml/QmlEmojiPickerPopup.h"

#include "cursor/CursorPlatformApplier.h"
#include "platform/WindowLevel.h"
#include "qml/EmojiPickerBackend.h"
#include "qml/QmlOverlayManager.h"

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QUrl>
#include <QWidget>
#include <QWindow>

namespace SnapTray {

namespace {

bool isPointerRefreshEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::Enter:
    case QEvent::HoverMove:
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        return true;
    default:
        return false;
    }
}

} // namespace

QmlEmojiPickerPopup::QmlEmojiPickerPopup(QObject* parent)
    : QObject(parent)
{
    if (auto* parentWidget = qobject_cast<QWidget*>(parent)) {
        m_parentWidget = parentWidget;
    }
}

QmlEmojiPickerPopup::~QmlEmojiPickerPopup()
{
    close();
}

void QmlEmojiPickerPopup::positionAt(const QRect& anchorRect)
{
    ensureView();
    if (!m_view) {
        return;
    }

    QScreen* screen = QGuiApplication::screenAt(anchorRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const QRect screenGeom = screen ? screen->availableGeometry() : QRect();
    const QSize popupSize = m_view->size();
    const int gap = 4;

    int x = anchorRect.left();
    int y = anchorRect.bottom() + gap;
    if (screen && y + popupSize.height() > screenGeom.bottom() - 10) {
        y = anchorRect.top() - popupSize.height() - gap;
    }

    if (screen) {
        x = qBound(screenGeom.left() + 10, x, screenGeom.right() - popupSize.width() - 10);
        y = qBound(screenGeom.top() + 10, y, screenGeom.bottom() - popupSize.height() - 10);
    }

    m_view->setPosition(x, y);
}

void QmlEmojiPickerPopup::showAt(const QRect& anchorRect)
{
    ensureView();
    if (!m_view) {
        return;
    }

    if (m_parentWidget) {
        QWidget* hostWindow = m_parentWidget->window();
        if (hostWindow && hostWindow->windowHandle()) {
            m_view->setTransientParent(hostWindow->windowHandle());
        }
    }

    positionAt(anchorRect);
    m_view->show();
    QmlOverlayManager::configureInteractiveOverlayWindow(m_view);
    applyPlatformWindowFlags();
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view);
    QmlOverlayManager::enableNativeShadow(m_view);
    m_view->raise();
    CursorPlatformApplier::applyWindowCursor(m_view, CursorStyleSpec::fromShape(Qt::ArrowCursor));
    emit cursorSyncRequested();
}

void QmlEmojiPickerPopup::hide()
{
    if (!m_view) {
        return;
    }

    m_view->hide();
    emit cursorRestoreRequested();
}

void QmlEmojiPickerPopup::close()
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

void QmlEmojiPickerPopup::setParentWidget(QWidget* parent)
{
    m_parentWidget = parent;
    if (!m_view) {
        return;
    }

    QWidget* hostWindow = m_parentWidget ? m_parentWidget->window() : nullptr;
    if (hostWindow && hostWindow->windowHandle()) {
        m_view->setTransientParent(hostWindow->windowHandle());
    } else {
        m_view->setTransientParent(nullptr);
    }
}

bool QmlEmojiPickerPopup::isVisible() const
{
    return m_view && m_view->isVisible();
}

QRect QmlEmojiPickerPopup::geometry() const
{
    if (!m_view) {
        return QRect();
    }
    return QRect(m_view->position(), m_view->size());
}

QWindow* QmlEmojiPickerPopup::window() const
{
    return m_view;
}

void QmlEmojiPickerPopup::applyPlatformWindowFlags()
{
    raiseTransientWindowAboveParent(m_view, m_parentWidget.data());
}

void QmlEmojiPickerPopup::ensureView()
{
    if (m_view) {
        return;
    }

    m_backend = new EmojiPickerBackend(this);
    m_view = QmlOverlayManager::instance().createPopupWindow();
    CursorPlatformApplier::applyWindowCursor(m_view, CursorStyleSpec::fromShape(Qt::ArrowCursor));
    m_view->rootContext()->setContextProperty(QStringLiteral("emojiPickerBackend"), m_backend);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/panels/EmojiPickerPopup.qml")));
    m_rootItem = m_view->rootObject();
    m_view->installEventFilter(this);

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors()) {
            qWarning() << "QmlEmojiPickerPopup QML error:" << error.toString();
        }
    }

    connect(m_backend, &EmojiPickerBackend::emojiSelected,
            this, &QmlEmojiPickerPopup::emojiSelected);

}

bool QmlEmojiPickerPopup::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_view) {
        if (isPointerRefreshEvent(event->type())) {
            emit cursorSyncRequested();
        }
        if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            emit cursorRestoreRequested();
        }
        return false;
    }

    if (!m_view || !m_view->isVisible()) {
        return false;
    }

    return false;
}

} // namespace SnapTray
