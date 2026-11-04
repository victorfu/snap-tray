#include "qml/QmlRecordingAreaSelector.h"

#include "platform/WindowLevel.h"
#include "qml/QmlOverlayManager.h"
#include "utils/VideoCropGeometry.h"

#include <QDebug>
#include <QQuickItem>
#include <QQuickView>
#include <QScopeGuard>
#include <QScreen>
#include <QtMath>

namespace SnapTray {

QmlRecordingAreaSelector::QmlRecordingAreaSelector(QObject* parent)
    : QObject(parent)
{
}

QmlRecordingAreaSelector::~QmlRecordingAreaSelector()
{
    close();
}

void QmlRecordingAreaSelector::start(QScreen* screen)
{
    close();
    if (!screen) return;
    m_screen = screen;
    m_view = QmlOverlayManager::instance().createScreenOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/components/RecordingAreaSelector.qml")));
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_rootItem = m_view->rootObject();
    if (!m_rootItem) {
        qWarning() << "QmlRecordingAreaSelector: RecordingAreaSelector.qml did not load";
        close();
        emit cancelled();
        return;
    }
    const qreal dpr = screen->devicePixelRatio() > 0.0 ? screen->devicePixelRatio() : 1.0;
    m_rootItem->setProperty("devicePixelRatio", dpr);
    m_rootItem->setProperty("minSide", qCeil(VideoCropGeometry::kMinCropSide / dpr));
    connect(m_rootItem, SIGNAL(accepted(qreal,qreal,qreal,qreal)), this, SLOT(onAccepted(qreal,qreal,qreal,qreal)));
    connect(m_rootItem, SIGNAL(cancelled()), this, SIGNAL(cancelled()));
    connect(m_rootItem, SIGNAL(fullScreenChosen()), this, SLOT(onFullScreenChosen()));
    // Closed by the window manager (Alt+F4): the same as Cancel, so the
    // recording setup ends instead of holding other captures (PR #205
    // review). Queued: the cancel handler closes this window again.
    connect(m_view, &QQuickWindow::closing, this, [this]() {
        if (m_closing) return;
        QMetaObject::invokeMethod(this, [this]() { emit cancelled(); }, Qt::QueuedConnection);
    });
    m_view->setScreen(screen);
    m_view->setGeometry(screen->geometry());
#ifdef Q_OS_LINUX
    // Full screen, so a window manager does not keep it clear of its panels.
    m_view->showFullScreen();
#else
    // Not showFullScreen(), like RegionSelector. macOS moves a full screen
    // window to a Space of its own, where the desktop to choose from is black.
    // On Windows the full-screen state change plus the overlay policy's frame
    // update expose the Qt Quick window before it can render, which crashes.
    m_view->show();
    raiseWindowAboveMenuBar(m_view);
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view);
#endif
    m_view->raise();
    m_view->requestActivate();
    m_rootItem->forceActiveFocus();
}

void QmlRecordingAreaSelector::close()
{
    if (!m_view) return;
    m_closing = true;
    const auto reset = qScopeGuard([this] { m_closing = false; });
    m_view->close();
    m_view->deleteLater();
    m_view = nullptr;
    m_rootItem = nullptr;
}

void QmlRecordingAreaSelector::onAccepted(qreal x, qreal y, qreal width, qreal height)
{
    const QRect screenGeometry = m_screen ? m_screen->geometry() : QRect();
    // From where the window really is (a window manager may have moved it).
    const QPoint origin = m_view ? m_view->position() : screenGeometry.topLeft();
    const QRect area(origin.x() + qRound(x), origin.y() + qRound(y), qRound(width), qRound(height));
    emit areaChosen(area.intersected(screenGeometry));
}

void QmlRecordingAreaSelector::onFullScreenChosen()
{
    if (m_screen) emit areaChosen(m_screen->geometry());
}

} // namespace SnapTray
