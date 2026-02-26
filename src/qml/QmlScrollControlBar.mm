#include "qml/QmlScrollControlBar.h"
#include "qml/QmlOverlayManager.h"

#include <QGuiApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QScreen>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace {

constexpr int kMargin = 12;
constexpr int kInsideBottomGap = 12;
constexpr int kOutsideGap = 10;

QPoint boundedTopLeft(const QPoint& candidate, const QSize& size, const QRect& screenRect)
{
    const int minX = screenRect.left() + kMargin;
    const int minY = screenRect.top() + kMargin;
    const int maxX = qMax(minX, screenRect.right() - size.width() - kMargin + 1);
    const int maxY = qMax(minY, screenRect.bottom() - size.height() - kMargin + 1);
    return QPoint(qBound(minX, candidate.x(), maxX),
                  qBound(minY, candidate.y(), maxY));
}

} // namespace

namespace SnapTray {

QmlScrollControlBar::QmlScrollControlBar(QObject* parent)
    : QObject(parent)
{
}

QmlScrollControlBar::~QmlScrollControlBar()
{
    if (m_view) {
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
    }
}

void QmlScrollControlBar::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/components/ScrollCaptureControlBar.qml")));

    // This bar needs mouse input for buttons and dragging — do NOT set
    // Qt::WindowTransparentForInput.  But prevent it from stealing focus.
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);

    m_rootItem = m_view->rootObject();
    connectSignals();
}

void QmlScrollControlBar::applyPlatformWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView* nsView = reinterpret_cast<NSView*>(m_view->winId());
    if (!nsView)
        return;

    NSWindow* window = [nsView window];
    if (!window)
        return;

    // Raise above the menu bar so the bar floats over everything.
    [window setLevel:kCGScreenSaverWindowLevel];

    // Prevent the bar from hiding when the app deactivates (LSUIElement).
    [window setHidesOnDeactivate:NO];
#endif
}

void QmlScrollControlBar::connectSignals()
{
    if (!m_rootItem)
        return;

    // Forward QML signals to C++ signals.
    connect(m_rootItem, SIGNAL(finishRequested()), this, SIGNAL(finishRequested()));
    connect(m_rootItem, SIGNAL(cancelRequested()), this, SIGNAL(cancelRequested()));
    connect(m_rootItem, SIGNAL(escapePressed()), this, SIGNAL(escapePressed()));
}

void QmlScrollControlBar::positionNear(const QRect& captureRegion, QScreen* screen)
{
    if (m_manualPositionSet)
        return;

    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    ensureView();

    const QSize barSize(m_view->width(), m_view->height());
    const QRect screenRect = screen->availableGeometry();

    const QList<QPoint> candidates{
        // Preferred: bottom-centered inside the selection rectangle.
        QPoint(captureRegion.center().x() - barSize.width() / 2,
               captureRegion.bottom() - barSize.height() - kInsideBottomGap + 1),
        // Fallback: just below the region.
        QPoint(captureRegion.center().x() - barSize.width() / 2,
               captureRegion.bottom() + kOutsideGap),
        // Fallback: just above the region.
        QPoint(captureRegion.center().x() - barSize.width() / 2,
               captureRegion.top() - barSize.height() - kOutsideGap),
        // Last resort: screen bottom center.
        QPoint(screenRect.center().x() - barSize.width() / 2,
               screenRect.bottom() - barSize.height() - kMargin + 1)
    };

    QPoint chosen = boundedTopLeft(candidates.first(), barSize, screenRect);
    for (const QPoint& candidate : candidates) {
        const QRect candidateRect(candidate, barSize);
        if (screenRect.contains(candidateRect)) {
            chosen = candidate;
            break;
        }
    }

    m_view->setPosition(chosen);
}

void QmlScrollControlBar::setSlowScrollWarning(bool active)
{
    ensureView();
    if (m_rootItem)
        m_rootItem->setProperty("slowScrollWarning", active);
}

bool QmlScrollControlBar::hasManualPosition() const
{
    return m_manualPositionSet;
}

void QmlScrollControlBar::show()
{
    ensureView();
    m_view->show();
    applyPlatformWindowFlags();

    // Detect manual drag: QML moves the QQuickView window directly.
    // Once the window position changes after the initial show, treat it
    // as a manual reposition.
    if (!m_manualPositionSet) {
        const QPoint initialPos = m_view->position();
        connect(m_view, &QWindow::xChanged, this, [this, initialPos]() {
            if (m_view && m_view->position() != initialPos)
                m_manualPositionSet = true;
        });
        connect(m_view, &QWindow::yChanged, this, [this, initialPos]() {
            if (m_view && m_view->position() != initialPos)
                m_manualPositionSet = true;
        });
    }
}

void QmlScrollControlBar::hide()
{
    if (m_view)
        m_view->hide();
}

void QmlScrollControlBar::close()
{
    if (m_view) {
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
        m_rootItem = nullptr;
    }
}

WId QmlScrollControlBar::winId() const
{
    if (m_view)
        return m_view->winId();
    return 0;
}

} // namespace SnapTray
