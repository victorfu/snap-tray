#include "qml/QmlToast.h"
#include "qml/QmlOverlayManager.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTimer>
#include <QWidget>

namespace SnapTray {

static const QUrl kToastQmlUrl(QStringLiteral("qrc:/SnapTrayQml/components/Toast.qml"));

// ============================================================================
// Screen-level singleton
// ============================================================================

QmlToast& QmlToast::screenToast()
{
    static QmlToast instance;
    return instance;
}

// Private constructor for screen-level toast
QmlToast::QmlToast()
    : QObject(nullptr)
    , m_anchorMode(AnchorMode::ScreenTopRight)
    , m_defaultAnchorMode(AnchorMode::ScreenTopRight)
    , m_isScreenLevel(true)
{
}

// Parent-anchored constructor
QmlToast::QmlToast(QWidget* parent, int shadowMargin)
    : QObject(parent)
    , m_anchorMode(AnchorMode::ParentTopCenter)
    , m_defaultAnchorMode(AnchorMode::ParentTopCenter)
    , m_shadowMargin(shadowMargin)
    , m_isScreenLevel(false)
    , m_parentWidget(parent)
{
}

QmlToast::~QmlToast()
{
    delete m_view;
}

// ============================================================================
// Public API
// ============================================================================

void QmlToast::showToast(Level level, const QString& title,
                         const QString& message, int durationMs)
{
    m_anchorMode = m_defaultAnchorMode;
    m_anchorRect = QRect();

    ensureView();
    setQmlProperties(level, title, message, durationMs);
    positionAndShow();
}

void QmlToast::showNearRect(Level level, const QString& title,
                            const QRect& anchorRect, int durationMs)
{
    m_anchorMode = AnchorMode::NearRect;
    m_anchorRect = anchorRect;

    ensureView();
    setQmlProperties(level, title, QString(), durationMs);
    positionAndShow();
}

// ============================================================================
// Internal
// ============================================================================

void QmlToast::ensureView()
{
    if (m_view)
        return;

    if (m_isScreenLevel) {
        m_view = QmlOverlayManager::instance().createScreenOverlay(kToastQmlUrl);
    } else {
        m_view = QmlOverlayManager::instance().createParentOverlay(kToastQmlUrl, m_parentWidget);
    }

    if (m_view->status() != QQuickView::Ready) {
        qWarning() << "QmlToast: Failed to load Toast.qml:" << m_view->errors();
    }

    m_rootItem = m_view->rootObject();
    if (!m_rootItem) {
        qWarning() << "QmlToast: rootObject is null after loading Toast.qml";
    }
}

void QmlToast::setQmlProperties(Level level, const QString& title,
                                const QString& message, int durationMs)
{
    if (!m_rootItem)
        return;

    bool isScreenLevel = (m_anchorMode == AnchorMode::ScreenTopRight);
    m_rootItem->setProperty("level", static_cast<int>(level));
    m_rootItem->setProperty("title", title);
    m_rootItem->setProperty("message", message);
    m_rootItem->setProperty("anchorMode", static_cast<int>(m_anchorMode));
    m_rootItem->setProperty("displayDuration", qMax(durationMs, 100));
    m_rootItem->setProperty("fixedWidth", isScreenLevel);
}

void QmlToast::positionAndShow()
{
    if (!m_view || !m_rootItem)
        return;

    // Defer positioning to let QML process property bindings (title, fixedWidth,
    // etc.) so width/height are up to date.  This avoids processEvents() which
    // can cause reentrancy issues.
    m_rootItem->polish();
    QTimer::singleShot(0, this, [this]() {
        if (!m_view || !m_rootItem)
            return;

        int toastW = qMax(qRound(m_rootItem->width()), 100);
        int toastH = qMax(qRound(m_rootItem->height()), 30);

        switch (m_anchorMode) {
        case AnchorMode::ScreenTopRight:
            positionScreenTopRight();
            break;
        case AnchorMode::ParentTopCenter:
            positionParentTopCenter();
            break;
        case AnchorMode::NearRect:
            positionNearRect();
            break;
        }

        m_view->resize(toastW, toastH);
        m_view->show();
        QmlOverlayManager::enableNativeShadow(m_view);
        m_view->raise();

        // Trigger the QML show() function
        QMetaObject::invokeMethod(m_rootItem, "show");
    });
}

void QmlToast::positionScreenTopRight()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    int toastW = qRound(m_rootItem->width());
    QRect geo = screen->availableGeometry();
    int x = geo.right() - toastW - kScreenMargin;
    int y = geo.top() + kScreenMargin;
    m_view->setPosition(x, y);
}

void QmlToast::positionParentTopCenter()
{
    if (!m_parentWidget) return;

    int toastW = qRound(m_rootItem->width());
    QPoint parentTopLeft = m_parentWidget->mapToGlobal(QPoint(0, 0));
    int x = parentTopLeft.x() + (m_parentWidget->width() - toastW) / 2;
    int y = parentTopLeft.y() + m_shadowMargin + 12;
    m_view->setPosition(x, y);
}

void QmlToast::positionNearRect()
{
    if (!m_parentWidget) return;

    int toastW = qRound(m_rootItem->width());
    int toastH = qRound(m_rootItem->height());

    if (m_anchorRect.isNull()) {
        // Fall back to parent center
        QPoint parentTopLeft = m_parentWidget->mapToGlobal(QPoint(0, 0));
        int x = parentTopLeft.x() + (m_parentWidget->width() - toastW) / 2;
        int y = parentTopLeft.y() + 12;
        m_view->setPosition(x, y);
        return;
    }

    // Map anchor rect to global coordinates
    QPoint anchorGlobal = m_parentWidget->mapToGlobal(m_anchorRect.topLeft());
    QRect globalAnchor(anchorGlobal, m_anchorRect.size());

    int x = globalAnchor.center().x() - toastW / 2;
    int y = globalAnchor.top() + 12;

    // Clamp within parent bounds (in global coordinates)
    QPoint parentTopLeft = m_parentWidget->mapToGlobal(QPoint(0, 0));
    int parentRight = parentTopLeft.x() + m_parentWidget->width();
    int parentBottom = parentTopLeft.y() + m_parentWidget->height();

    x = qBound(parentTopLeft.x() + 4, x, parentRight - toastW - 4);
    y = qBound(parentTopLeft.y() + 4, y, parentBottom - toastH - 4);

    m_view->setPosition(x, y);
}

} // namespace SnapTray
