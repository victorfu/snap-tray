#include "pinwindow/PinHoverBar.h"

#include "qml/PinHoverBarViewModel.h"
#include "qml/QmlOverlayManager.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QtMath>

using SnapTray::QmlOverlayManager;

namespace {
QString panelButtonName(int panel)
{
    return panel == PinHoverBarViewModel::OpacityPanel ? QStringLiteral("pinHoverOpacity")
                                                       : QStringLiteral("pinHoverZoom");
}
} // namespace

PinHoverBar::PinHoverBar(PinHoverBarViewModel* viewModel, QWindow* owner, QObject* parent)
    : QObject(parent)
    , m_viewModel(viewModel)
    , m_owner(owner)
{
    m_showTimer.setSingleShot(true);
    m_showTimer.setInterval(kShowDelayMs);
    connect(&m_showTimer, &QTimer::timeout, this, &PinHoverBar::onShowTimeout);
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(kHideDelayMs);
    connect(&m_hideTimer, &QTimer::timeout, this, &PinHoverBar::onHideTimeout);
    if (m_viewModel)
        connect(m_viewModel, &PinHoverBarViewModel::panelChanged, this, &PinHoverBar::onPanelChanged);
}

PinHoverBar::~PinHoverBar()
{
    if (m_filterInstalled) qApp->removeEventFilter(this);
    for (QQuickView* view : {m_panelView, m_barView}) {
        if (!view) continue;
        view->close();
        delete view;
    }
}

void PinHoverBar::setOwner(QWindow* owner)
{
    if (m_owner == owner) return;
    m_owner = owner;
    for (QQuickView* view : {m_barView, m_panelView}) {
        if (view) view->setTransientParent(owner);
    }
}

void PinHoverBar::setCursorPositionProvider(std::function<QPoint()> provider)
{
    m_cursorPos = std::move(provider);
}

QPoint PinHoverBar::cursorPos() const
{
    return m_cursorPos ? m_cursorPos() : QCursor::pos();
}

bool PinHoverBar::ensureBar()
{
    if (m_barView) return true;
    if (!m_viewModel) return false;
    m_barView = QmlOverlayManager::instance().createToolPanelWindow();
    m_barView->setTransientParent(m_owner);
    m_barView->setResizeMode(QQuickView::SizeViewToRootObject);
    m_barView->setInitialProperties({{"viewModel", QVariant::fromValue(static_cast<QObject*>(m_viewModel.data()))}});
    m_barView->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/pin/PinHoverBar.qml")));
    m_barRoot = m_barView->rootObject();
    if (!m_barRoot) {
        qWarning() << "Pin hover bar failed to load:" << m_barView->errors();
        delete m_barView;
        m_barView = nullptr;
        return false;
    }
    // The bar's width follows its content (compact layout, labels); a value
    // change can make the full bar too wide for the pin.
    connect(m_barRoot, &QQuickItem::widthChanged, this, &PinHoverBar::positionBar);
    connect(m_viewModel, &PinHoverBarViewModel::valuesChanged, this, &PinHoverBar::updateCompact);
    connect(m_viewModel, &PinHoverBarViewModel::statusChanged, this, &PinHoverBar::updateCompact);
    return true;
}

bool PinHoverBar::ensurePanel()
{
    if (m_panelView) return true;
    if (!m_viewModel) return false;
    m_panelView = QmlOverlayManager::instance().createPopupWindow();
    m_panelView->setTransientParent(m_owner);
    m_panelView->setResizeMode(QQuickView::SizeViewToRootObject);
    m_panelView->setInitialProperties({{"viewModel", QVariant::fromValue(static_cast<QObject*>(m_viewModel.data()))}});
    m_panelView->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/pin/PinValuePanel.qml")));
    m_panelRoot = m_panelView->rootObject();
    if (!m_panelRoot) {
        qWarning() << "Pin value panel failed to load:" << m_panelView->errors();
        delete m_panelView;
        m_panelView = nullptr;
        return false;
    }
    connect(m_panelRoot, &QQuickItem::heightChanged, this, &PinHoverBar::positionPanel);
    return true;
}

void PinHoverBar::setPinGeometry(const QRect& contentGlobal)
{
    m_pin = contentGlobal;
    updateCompact();
    positionBar();
}

void PinHoverBar::updateCompact()
{
    if (!m_barRoot || !m_viewModel || m_pin.isEmpty()) return;
    const qreal full = m_barRoot->property("fullWidth").toReal();
    m_viewModel->setCompact(full > m_pin.width() - 2 * kBottomInset);
}

bool PinHoverBar::barFits() const
{
    // The bar keeps the same inset on the sides as at the bottom; labels
    // differ by language, so this is measured, not a fixed width.
    if (!m_barRoot || !m_viewModel) return false;
    const char* mode = m_viewModel->compact() ? "compactWidth" : "fullWidth";
    return qCeil(m_barRoot->property(mode).toReal()) <= m_pin.width() - 2 * kBottomInset
        && qCeil(m_barRoot->implicitHeight()) <= m_pin.height() - 2 * kBottomInset;
}

void PinHoverBar::positionBar()
{
    if (!m_barView || m_pin.isEmpty()) return;
    if (isBarVisible() && !barFits()) {
        hideNow();
        return;
    }
    // Sized from the bound width of the current mode: the Row re-lays out
    // a frame later, and the bar must be right on the first frame.
    QSize size = m_barView->size();
    if (m_barRoot && m_viewModel) {
        const char* mode = m_viewModel->compact() ? "compactWidth" : "fullWidth";
        size.setWidth(qCeil(m_barRoot->property(mode).toReal()));
    }
    // Centred inside the pin's bottom edge, kBottomInset above it.
    m_barView->setGeometry(m_pin.center().x() - (size.width() - 1) / 2,
                           m_pin.bottom() - kBottomInset - size.height() + 1,
                           size.width(), size.height());
    positionPanel();
}

void PinHoverBar::positionPanel()
{
    if (!m_panelView || !m_panelView->isVisible() || !m_barView) return;
    QVariant rect;
    QMetaObject::invokeMethod(m_barRoot, "buttonRect", Q_RETURN_ARG(QVariant, rect),
                              Q_ARG(QVariant, panelButtonName(m_lastPanel)));
    const QRectF button = rect.toRectF();
    const QRect bar = barGeometry();
    const QSize size = m_panelView->size();
    const int anchorX = bar.x() + qRound(button.x() + button.width() / 2);
    QPoint pos(anchorX - size.width() / 2, bar.top() - kPanelGap - size.height());
    QScreen* screen = m_owner && m_owner->screen() ? m_owner->screen() : QGuiApplication::primaryScreen();
    if (screen) {
        const QRect area = screen->availableGeometry();
        pos.setX(qBound(area.left(), pos.x(), area.right() - size.width() + 1));
        if (pos.y() < area.top()) pos.setY(bar.bottom() + 1 + kPanelGap);   // no room above
    }
    m_panelView->setPosition(pos);
}

void PinHoverBar::pointerEntered()
{
    m_hideTimer.stop();
    if (m_suppressed || isBarVisible()) return;
    m_showTimer.start();
}

void PinHoverBar::pointerLeft()
{
    m_showTimer.stop();
    if (isBarVisible()) m_hideTimer.start();
}

void PinHoverBar::onShowTimeout()
{
    if (m_suppressed || !containsGlobal(cursorPos())) return;
    showNow();
}

void PinHoverBar::onHideTimeout()
{
    // The pin gets a leave event when the pointer moves onto the bar or the
    // panel; those keep the bar.
    if (containsGlobal(cursorPos())) {
        m_hideTimer.start();
        return;
    }
    hideNow();
}

void PinHoverBar::showNow()
{
    if (m_suppressed || m_pin.isEmpty() || !m_viewModel) return;
    if (!ensureBar()) return;
    updateCompact();
    m_hideTimer.stop();
    if (m_barView->isVisible()) return;
    if (!barFits()) return;
    positionBar();
    m_barView->show();
    QmlOverlayManager::applyShownOverlayWindowPolicy(m_barView);
    m_barView->raise();
    emit barShown();
}

void PinHoverBar::hideNow()
{
    m_showTimer.stop();
    m_hideTimer.stop();
    hidePanel();
    if (m_barView && m_barView->isVisible()) {
        m_barView->hide();
        emit barHidden();
    }
}

void PinHoverBar::setSuppressed(bool suppressed)
{
    const bool lifted = m_suppressed && !suppressed;
    m_suppressed = suppressed;
    if (suppressed) {
        hideNow();
        return;
    }
    // After a click, drag, toolbar or layout session the pointer may still be
    // on the pin: the bar comes back without leaving and re-entering.
    if (lifted && !isBarVisible() && containsGlobal(cursorPos())) m_showTimer.start();
}

bool PinHoverBar::isBarVisible() const { return m_barView && m_barView->isVisible(); }
bool PinHoverBar::isPanelVisible() const { return m_panelView && m_panelView->isVisible(); }
QWindow* PinHoverBar::barWindow() const { return m_barView; }
QWindow* PinHoverBar::panelWindow() const { return m_panelView; }

QRect PinHoverBar::barGeometry() const
{
    return m_barView ? m_barView->geometry() : QRect();
}

bool PinHoverBar::containsGlobal(const QPoint& pos) const
{
    return m_pin.contains(pos) || (isBarVisible() && m_barView->geometry().contains(pos))
        || (isPanelVisible() && m_panelView->geometry().contains(pos));
}

void PinHoverBar::focusButton(const QString& name)
{
    if (!m_barRoot) return;
    QMetaObject::invokeMethod(m_barRoot, "focusButton", Q_ARG(QVariant, name));
}

void PinHoverBar::onPanelChanged()
{
    const int panel = m_viewModel ? m_viewModel->panel() : 0;
    if (panel == PinHoverBarViewModel::NoPanel) {
        hidePanel();
        return;
    }
    m_lastPanel = panel;
    if (!isBarVisible() || !ensurePanel()) {
        m_viewModel->closePanel();
        return;
    }
    m_panelView->show();
    QmlOverlayManager::applyShownOverlayWindowPolicy(m_panelView);
    QmlOverlayManager::enableNativeShadow(m_panelView);
    positionPanel();
    m_panelView->raise();
    if (!m_filterInstalled) {
        qApp->installEventFilter(this);
        m_filterInstalled = true;
    }
}

void PinHoverBar::hidePanel()
{
    if (m_filterInstalled) {
        qApp->removeEventFilter(this);
        m_filterInstalled = false;
    }
    const bool wasVisible = isPanelVisible();
    if (wasVisible) m_panelView->hide();
    if (m_viewModel) m_viewModel->closePanel();   // re-enters with NoPanel: no-op
    if (wasVisible && isBarVisible()) focusButton(panelButtonName(m_lastPanel));
}

bool PinHoverBar::eventFilter(QObject* obj, QEvent* event)
{
    if (!isPanelVisible()) return QObject::eventFilter(obj, event);
    switch (event->type()) {
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress: {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() != Qt::Key_Escape) {
            // The panel is pointer-only: any other key closes it and then
            // reaches the pin as usual (no rotating behind an open panel).
            if (event->type() == QEvent::KeyPress) hidePanel();
            break;
        }
        key->accept();
        if (event->type() == QEvent::KeyPress) hidePanel();
        return true;
    }
    case QEvent::MouseButtonPress: {
        const QPoint pos = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        if (m_panelView->geometry().contains(pos) || barGeometry().contains(pos)) break;
        hidePanel();
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(obj, event);
}
