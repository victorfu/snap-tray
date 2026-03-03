#include "qml/QmlBadge.h"
#include "qml/QmlOverlayManager.h"

#include <QQuickWidget>
#include <QQuickItem>
#include <QQmlEngine>
#include <QUrl>

namespace SnapTray {

static const QUrl kBadgeQmlUrl(QStringLiteral("qrc:/SnapTrayQml/components/Badge.qml"));

QmlBadge::QmlBadge(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void QmlBadge::ensureInitialized()
{
    if (m_initialized)
        return;
    m_initialized = true;

    // Use the shared QML engine from QmlOverlayManager
    auto* engine = QmlOverlayManager::instance().engine();

    m_quickWidget = new QQuickWidget(engine, this);
    m_quickWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_quickWidget->setAttribute(Qt::WA_TranslucentBackground);
    m_quickWidget->setAttribute(Qt::WA_AlwaysStackOnTop);
    m_quickWidget->setClearColor(Qt::transparent);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);

    m_quickWidget->setSource(kBadgeQmlUrl);

    if (m_quickWidget->status() != QQuickWidget::Ready) {
        qWarning() << "QmlBadge: Failed to load Badge.qml:" << m_quickWidget->errors();
        return;
    }

    m_rootItem = m_quickWidget->rootObject();

    // Connect the QML hidden() signal to hide the C++ widget after fade-out
    if (m_rootItem) {
        connect(m_rootItem, SIGNAL(hidden()), this, SLOT(onQmlHidden()));
    }
}

void QmlBadge::showBadge(const QString& text, int durationMs)
{
    ensureInitialized();
    if (!m_rootItem)
        return;

    m_rootItem->setProperty("badgeText", text);
    m_rootItem->setProperty("duration", durationMs);

    if (!m_logicallyVisible) {
        // First show: trigger fade-in
        m_rootItem->setProperty("badgeVisible", true);
        m_logicallyVisible = true;

        // Size the widget to match QML content, then show
        int w = qRound(m_rootItem->implicitWidth());
        int h = qRound(m_rootItem->implicitHeight());
        setFixedSize(w, h);
        m_quickWidget->setFixedSize(w, h);
        show();
        raise();
    } else {
        // Already visible: update text, restart timer (no re-fade-in)
        // Resize in case text width changed
        int w = qRound(m_rootItem->implicitWidth());
        int h = qRound(m_rootItem->implicitHeight());
        setFixedSize(w, h);
        m_quickWidget->setFixedSize(w, h);

        QMetaObject::invokeMethod(m_rootItem, "retrigger");
    }
}

void QmlBadge::hideBadge()
{
    if (!m_rootItem || !m_logicallyVisible)
        return;

    m_rootItem->setProperty("badgeVisible", false);
    m_logicallyVisible = false;
}

void QmlBadge::onQmlHidden()
{
    if (!m_logicallyVisible)
        hide();
}

} // namespace SnapTray
