#include "qml/QmlRecordingBoundary.h"
#include "qml/QmlOverlayManager.h"

#include <QQuickView>
#include <QQuickItem>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace SnapTray {

QmlRecordingBoundary::QmlRecordingBoundary(QObject* parent)
    : QObject(parent)
{
}

QmlRecordingBoundary::~QmlRecordingBoundary()
{
    if (m_view) {
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
    }
}

void QmlRecordingBoundary::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/components/RecordingBoundary.qml")));

    // Make click-through at the Qt level
    m_view->setFlag(Qt::WindowTransparentForInput, true);

    m_rootItem = m_view->rootObject();
}

void QmlRecordingBoundary::applyPlatformWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView* view = reinterpret_cast<NSView*>(m_view->winId());
    if (!view)
        return;

    NSWindow* window = [view window];
    if (!window)
        return;

    // Raise above menu bar (same as raiseWindowAboveMenuBar)
    [window setLevel:kCGScreenSaverWindowLevel]; // level 1000

    // Click-through at the native level (belt-and-suspenders with Qt flag)
    [window setIgnoresMouseEvents:YES];
#endif
}

void QmlRecordingBoundary::setRegion(const QRect& region)
{
    m_region = region;
    ensureView();

    if (m_rootItem) {
        m_rootItem->setProperty("regionWidth", region.width());
        m_rootItem->setProperty("regionHeight", region.height());
    }

    // Position the overlay so the border+glow surrounds the recording region.
    const int margin = kBorderWidth + kGlowPadding;
    m_view->setGeometry(
        region.x() - margin,
        region.y() - margin,
        region.width() + 2 * margin,
        region.height() + 2 * margin);
}

void QmlRecordingBoundary::setBorderMode(BorderMode mode)
{
    if (m_borderMode == mode)
        return;

    m_borderMode = mode;
    ensureView();

    if (m_rootItem) {
        m_rootItem->setProperty("borderMode", static_cast<int>(mode));
    }
}

void QmlRecordingBoundary::show()
{
    ensureView();
    m_view->show();
    applyPlatformWindowFlags();
}

void QmlRecordingBoundary::hide()
{
    if (m_view)
        m_view->hide();
}

void QmlRecordingBoundary::close()
{
    if (m_view) {
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
        m_rootItem = nullptr;
    }
}

void QmlRecordingBoundary::raiseAboveMenuBar()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView* view = reinterpret_cast<NSView*>(m_view->winId());
    if (view) {
        NSWindow* window = [view window];
        [window setLevel:kCGScreenSaverWindowLevel];
    }
#endif
}

void QmlRecordingBoundary::setExcludedFromCapture(bool excluded)
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView* view = reinterpret_cast<NSView*>(m_view->winId());
    if (view) {
        NSWindow* window = [view window];
        if (window) {
            [window setSharingType:excluded ? NSWindowSharingNone : NSWindowSharingReadOnly];
        }
    }
#else
    Q_UNUSED(excluded)
#endif
}

WId QmlRecordingBoundary::winId() const
{
    if (m_view)
        return m_view->winId();
    return 0;
}

} // namespace SnapTray
