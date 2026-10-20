#include "qml/QmlDelayedCaptureIndicator.h"
#include "qml/QmlOverlayManager.h"

#include <QDebug>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>

namespace SnapTray {

namespace {
const QUrl kIndicatorQmlUrl(
    QStringLiteral("qrc:/SnapTrayQml/components/DelayedCaptureIndicator.qml"));
}

QmlDelayedCaptureIndicator::QmlDelayedCaptureIndicator(QObject* parent)
    : QObject(parent)
{
}

QmlDelayedCaptureIndicator::~QmlDelayedCaptureIndicator()
{
    if (m_view) {
        m_view->close();
        m_rootItem = nullptr;
        // The view may still be referenced by the scene graph thread; let the
        // event loop release it, matching the other QML overlays.
        m_view->deleteLater();
        m_view = nullptr;
    }
}

void QmlDelayedCaptureIndicator::ensureView()
{
    if (m_view) {
        return;
    }

    m_view = QmlOverlayManager::instance().createScreenOverlay(kIndicatorQmlUrl);
    // Never steal focus or input: the user interacts with other apps during the countdown.
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_view->setFlag(Qt::WindowTransparentForInput, true);

    if (m_view->status() != QQuickView::Ready) {
        qWarning() << "QmlDelayedCaptureIndicator: Failed to load DelayedCaptureIndicator.qml:"
                   << m_view->errors();
    }

    m_rootItem = m_view->rootObject();
    if (!m_rootItem) {
        qWarning() << "QmlDelayedCaptureIndicator: rootObject is null";
        return;
    }

    connect(m_rootItem, &QQuickItem::implicitWidthChanged,
            this, &QmlDelayedCaptureIndicator::reposition);
    connect(m_rootItem, &QQuickItem::implicitHeightChanged,
            this, &QmlDelayedCaptureIndicator::reposition);
}

void QmlDelayedCaptureIndicator::show(QScreen* screen, int remainingSeconds)
{
    ensureView();
    if (!m_view || !m_rootItem || !screen) {
        return;
    }

    m_screen = screen;
    m_rootItem->setProperty("remainingSeconds", remainingSeconds);
    m_view->setScreen(screen);
    reposition();
    m_view->show();
}

void QmlDelayedCaptureIndicator::setRemainingSeconds(int seconds)
{
    if (m_rootItem) {
        m_rootItem->setProperty("remainingSeconds", seconds);
    }
}

void QmlDelayedCaptureIndicator::hide()
{
    if (m_view) {
        m_view->hide();
    }
}

bool QmlDelayedCaptureIndicator::isVisible() const
{
    return m_view && m_view->isVisible();
}

QWindow* QmlDelayedCaptureIndicator::window() const
{
    return m_view;
}

void QmlDelayedCaptureIndicator::reposition()
{
    if (!m_view || !m_rootItem || !m_screen) {
        return;
    }

    const QRect area = m_screen->availableGeometry();
    const int width = qRound(m_rootItem->implicitWidth());
    const int height = qRound(m_rootItem->implicitHeight());
    m_view->setPosition(area.center().x() - width / 2, area.top() + kTopMargin);
}

} // namespace SnapTray
