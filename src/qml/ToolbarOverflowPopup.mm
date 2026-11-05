#include "qml/ToolbarOverflowPopup.h"

#include "cursor/CursorSurfaceSupport.h"
#include "qml/QmlOverlayManager.h"
#include "qml/ToolbarOverflowLayout.h"

#include <QCoreApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QVariant>

namespace SnapTray {

namespace {
constexpr int kAnchorGap = 4;
}

ToolbarOverflowPopup::ToolbarOverflowPopup(QObject* viewModel, QWindow* owner, QObject* parent)
    : QObject(parent)
    , m_viewModel(viewModel)
    , m_owner(owner)
{
}

ToolbarOverflowPopup::~ToolbarOverflowPopup()
{
    if (m_filterInstalled) qApp->removeEventFilter(this);
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->close();
        delete m_view;
    }
}

void ToolbarOverflowPopup::setKeepOpenTest(std::function<bool(const QPoint&)> test)
{
    m_keepOpen = std::move(test);
}

void ToolbarOverflowPopup::setCursorOwnerId(const QString& ownerId)
{
    m_cursorOwnerId = ownerId;
}

bool ToolbarOverflowPopup::ensureView()
{
    if (m_view) return true;
    m_view = QmlOverlayManager::instance().createPopupWindow();
    m_view->setTransientParent(m_owner);
    m_view->setInitialProperties({{"viewModel", QVariant::fromValue(m_viewModel.data())}});
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolbarOverflowMenu.qml")));
    m_rootItem = m_view->rootObject();
    if (!m_rootItem) {
        qWarning() << "Toolbar overflow menu failed to load:" << m_view->errors();
        m_view->close();
        delete m_view;
        m_view = nullptr;
        return false;
    }
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("ToolbarOverflow"));
    connect(m_rootItem, SIGNAL(actionTriggered(int)), this, SLOT(onActionTriggered(int)));
    connect(m_view, &QWindow::heightChanged, this, &ToolbarOverflowPopup::position);
    return true;
}

void ToolbarOverflowPopup::toggle(const QVariantList& buttons, const QRect& anchorGlobal,
                                  const QRect& bounds)
{
    if (isVisible()) {
        hide();
        return;
    }
    if (buttons.isEmpty() || bounds.isEmpty() || !ensureView()) return;
    m_anchor = anchorGlobal;
    m_bounds = bounds;
    m_rootItem->setProperty("maximumWidth", bounds.width());
    m_rootItem->setProperty("maximumHeight", bounds.height());
    m_rootItem->setProperty("selectedIndex", -1);
    m_rootItem->setProperty("buttons", buttons);
    position();
    m_view->show();
    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
    QmlOverlayManager::enableNativeShadow(m_view);
    m_view->raise();
    if (!m_filterInstalled) {
        qApp->installEventFilter(this);
        m_filterInstalled = true;
    }
}

void ToolbarOverflowPopup::hide()
{
    if (m_filterInstalled) {
        qApp->removeEventFilter(this);
        m_filterInstalled = false;
    }
    if (!m_view || !m_view->isVisible()) return;
    m_view->hide();
    CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
    emit closed();
}

bool ToolbarOverflowPopup::isVisible() const
{
    return m_view && m_view->isVisible();
}

QWindow* ToolbarOverflowPopup::window() const
{
    return m_view;
}

void ToolbarOverflowPopup::position()
{
    if (!m_view || m_bounds.isEmpty()) return;
    const QSize size = m_view->size();
    int y = m_anchor.bottom() + 1 + kAnchorGap;
    if (y + size.height() > m_bounds.y() + m_bounds.height())
        y = m_anchor.top() - kAnchorGap - size.height();
    m_view->setPosition(boundToolbarPosition(
        QPoint(m_anchor.right() + 1 - size.width(), y), size, m_bounds));
}

void ToolbarOverflowPopup::onActionTriggered(int buttonId)
{
    hide();
    if (m_viewModel)
        QMetaObject::invokeMethod(m_viewModel, "handleButtonClicked", Q_ARG(int, buttonId));
}

bool ToolbarOverflowPopup::handleKey(QKeyEvent* key)
{
    if (!isVisible() || !key) return false;
    const int code = key->key();
    if (code != Qt::Key_Escape && code != Qt::Key_Up && code != Qt::Key_Down
        && code != Qt::Key_Return && code != Qt::Key_Enter)
        return false;
    key->accept();
    if (key->type() == QEvent::ShortcutOverride) return true;
    if (code == Qt::Key_Escape) {
        hide();
    } else if (code == Qt::Key_Up || code == Qt::Key_Down) {
        QMetaObject::invokeMethod(m_rootItem, "moveSelection",
                                  Q_ARG(QVariant, code == Qt::Key_Down ? 1 : -1));
    } else {
        QMetaObject::invokeMethod(m_rootItem, "activateSelected");
    }
    return true;
}

bool ToolbarOverflowPopup::eventFilter(QObject* obj, QEvent* event)
{
    if (!isVisible()) return QObject::eventFilter(obj, event);
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::ShortcutOverride:
        return handleKey(static_cast<QKeyEvent*>(event));
    case QEvent::MouseButtonPress: {
        const QPoint pos = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        if (m_view->geometry().contains(pos)) break;
        if (m_keepOpen && m_keepOpen(pos)) break;
        hide();
        break;
    }
    default:
        break;
    }
    if (obj == m_view) {
        CursorSurfaceSupport::syncWindowSurface(m_view, m_cursorSurfaceId, m_cursorOwnerId,
                                                CursorRequestSource::Popup);
    }
    return QObject::eventFilter(obj, event);
}

} // namespace SnapTray
