#include "qml/QmlScrollPreviewWindow.h"
#include "qml/QmlOverlayManager.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickView>
#include <QQuickItem>
#include <QScreen>

#include <limits>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace {

constexpr int kMargin = 12;
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

int overlapArea(const QRect& a, const QRect& b)
{
    const QRect inter = a.intersected(b);
    if (inter.isEmpty())
        return 0;
    return inter.width() * inter.height();
}

} // namespace

namespace SnapTray {

// ---------------------------------------------------------------------------
// ScrollPreviewImageProvider
// ---------------------------------------------------------------------------

ScrollPreviewImageProvider::ScrollPreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage ScrollPreviewImageProvider::requestImage(const QString& /*id*/, QSize* size,
                                                 const QSize& requestedSize)
{
    QMutexLocker lock(&m_mutex);

    if (m_image.isNull()) {
        if (size)
            *size = QSize(0, 0);
        return {};
    }

    if (size)
        *size = m_image.size();

    if (requestedSize.isValid() && requestedSize != m_image.size())
        return m_image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return m_image;
}

void ScrollPreviewImageProvider::updateImage(const QImage& image)
{
    QMutexLocker lock(&m_mutex);
    m_image = image;
}

// ---------------------------------------------------------------------------
// QmlScrollPreviewWindow
// ---------------------------------------------------------------------------

QmlScrollPreviewWindow::QmlScrollPreviewWindow(QObject* parent)
    : QObject(parent)
{
}

QmlScrollPreviewWindow::~QmlScrollPreviewWindow()
{
    if (m_view) {
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
    }
}

void QmlScrollPreviewWindow::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/components/ScrollCapturePreviewWindow.qml")));

    // Click-through: the preview is purely informational.
    m_view->setFlag(Qt::WindowTransparentForInput, true);
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);

    // Register the image provider on this view's engine.
    // QQmlEngine takes ownership of the provider.
    m_imageProvider = new ScrollPreviewImageProvider();
    m_view->engine()->addImageProvider(QStringLiteral("scrollpreview"), m_imageProvider);

    m_rootItem = m_view->rootObject();
}

void QmlScrollPreviewWindow::applyPlatformWindowFlags()
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

    // Raise above menu bar.
    [window setLevel:kCGScreenSaverWindowLevel];

    // Click-through at the native level (belt-and-suspenders with Qt flag).
    [window setIgnoresMouseEvents:YES];

    // Prevent hiding when the app deactivates (LSUIElement).
    [window setHidesOnDeactivate:NO];
#endif
}

void QmlScrollPreviewWindow::setPreviewImage(const QImage& preview)
{
    ensureView();

    if (!m_imageProvider || preview.isNull())
        return;

    m_imageProvider->updateImage(preview);
    ++m_imageGeneration;

    // Update the QML image source with a new query parameter to force reload.
    if (m_rootItem) {
        const QString source = QStringLiteral("image://scrollpreview/current?t=%1")
                                   .arg(m_imageGeneration);
        m_rootItem->setProperty("previewSource", source);
    }
}

void QmlScrollPreviewWindow::positionNear(const QRect& captureRegion, QScreen* screen)
{
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    ensureView();

    const QSize windowSize(m_view->width(), m_view->height());
    const QRect screenRect = screen->availableGeometry();
    const QRect avoidRect = captureRegion.adjusted(-2, -2, 2, 2);

    const QList<QPoint> candidates{
        // Prefer right side, vertically center-aligned with capture.
        QPoint(captureRegion.right() + 1 + kOutsideGap,
               captureRegion.center().y() - windowSize.height() / 2),
        QPoint(captureRegion.right() + 1 + kOutsideGap,
               captureRegion.bottom() - windowSize.height() + 1),
        QPoint(captureRegion.right() + 1 + kOutsideGap,
               captureRegion.top()),
        // Then left side.
        QPoint(captureRegion.left() - windowSize.width() - kOutsideGap,
               captureRegion.center().y() - windowSize.height() / 2),
        QPoint(captureRegion.left() - windowSize.width() - kOutsideGap,
               captureRegion.bottom() - windowSize.height() + 1),
        QPoint(captureRegion.left() - windowSize.width() - kOutsideGap,
               captureRegion.top())
    };

    QPoint best = boundedTopLeft(candidates.first(), windowSize, screenRect);
    int bestOverlap = std::numeric_limits<int>::max();
    for (const QPoint& candidate : candidates) {
        const QPoint bounded = boundedTopLeft(candidate, windowSize, screenRect);
        const QRect candidateRect(bounded, windowSize);
        const int overlap = overlapArea(candidateRect, avoidRect);
        if (overlap == 0) {
            m_view->setPosition(bounded);
            return;
        }
        if (overlap < bestOverlap) {
            bestOverlap = overlap;
            best = bounded;
        }
    }

    // Degenerate case (capture region almost full-screen): choose least-overlap spot.
    m_view->setPosition(best);
}

void QmlScrollPreviewWindow::show()
{
    ensureView();
    m_view->show();
    applyPlatformWindowFlags();
}

void QmlScrollPreviewWindow::hide()
{
    if (m_view)
        m_view->hide();
}

void QmlScrollPreviewWindow::close()
{
    if (m_view) {
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
        m_rootItem = nullptr;
        m_imageProvider = nullptr;  // owned by engine, destroyed with view
    }
}

WId QmlScrollPreviewWindow::winId() const
{
    if (m_view)
        return m_view->winId();
    return 0;
}

} // namespace SnapTray
