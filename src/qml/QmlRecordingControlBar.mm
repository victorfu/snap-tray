#include "qml/QmlRecordingControlBar.h"
#include "qml/QmlOverlayManager.h"
#include "ToolbarStyle.h"
#include "ui/GlassTooltip.h"
#include "platform/WindowLevel.h"

#include <QQuickView>
#include <QQuickItem>
#include <QShortcut>
#include <QScreen>
#include <QGuiApplication>
#include <QtMath>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace SnapTray {

QmlRecordingControlBar::QmlRecordingControlBar(QObject* parent)
    : QObject(parent)
{
    ensureView();
}

QmlRecordingControlBar::~QmlRecordingControlBar()
{
    if (m_tooltip) {
        m_tooltip->deleteLater();
        m_tooltip = nullptr;
    }

    delete m_escShortcut;
    m_escShortcut = nullptr;

    if (m_view) {
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
        m_rootItem = nullptr;
    }
}

void QmlRecordingControlBar::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingControlBar.qml")));

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "RecordingControlBar QML error:" << error.toString();
    }

    m_rootItem = m_view->rootObject();

    if (m_rootItem) {
        updateThemeColors();
        setupConnections();
    }

    // ESC shortcut — global application shortcut, managed in C++ (not tied to QML focus)
    m_escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_view);
    m_escShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_escShortcut, &QShortcut::activated, this, &QmlRecordingControlBar::stopRequested);

    // SizeViewToRootObject handles view ↔ root sizing automatically
}

void QmlRecordingControlBar::setupConnections()
{
    if (!m_rootItem)
        return;

    // QML signals → C++ signals
    connect(m_rootItem, SIGNAL(stopRequested()), this, SIGNAL(stopRequested()));
    connect(m_rootItem, SIGNAL(cancelRequested()), this, SIGNAL(cancelRequested()));
    connect(m_rootItem, SIGNAL(pauseRequested()), this, SIGNAL(pauseRequested()));
    connect(m_rootItem, SIGNAL(resumeRequested()), this, SIGNAL(resumeRequested()));

    // Tooltip signals
    connect(m_rootItem, SIGNAL(buttonHovered(int,qreal,qreal,qreal,qreal)),
            this, SLOT(onButtonHovered(int,qreal,qreal,qreal,qreal)));
    connect(m_rootItem, SIGNAL(buttonUnhovered()),
            this, SLOT(onButtonUnhovered()));

    // Drag signals
    connect(m_rootItem, SIGNAL(dragStarted()), this, SLOT(onDragStarted()));
    connect(m_rootItem, SIGNAL(dragFinished()), this, SLOT(onDragFinished()));
    connect(m_rootItem, SIGNAL(dragMoved(qreal,qreal)), this, SLOT(onDragMoved(qreal,qreal)));

    // Width change signal
    connect(m_rootItem, SIGNAL(contentWidthChanged()), this, SLOT(onWidthChanged()));
}

void QmlRecordingControlBar::updateThemeColors()
{
    if (!m_rootItem)
        return;

    const auto& config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    // Glass background: top color is alpha+20
    QColor topColor = config.glassBackgroundColor;
    topColor.setAlpha(qMin(255, topColor.alpha() + 20));

    m_rootItem->setProperty("themeGlassBg", config.glassBackgroundColor);
    m_rootItem->setProperty("themeGlassBgTop", topColor);
    m_rootItem->setProperty("themeHighlight", config.glassHighlightColor);
    m_rootItem->setProperty("themeBorder", config.hairlineBorderColor);
    m_rootItem->setProperty("themeText", config.textColor);
    m_rootItem->setProperty("themeSeparator", config.separatorColor);
    m_rootItem->setProperty("themeHoverBg", config.hoverBackgroundColor);
    m_rootItem->setProperty("themeIconNormal", config.iconNormalColor);
    m_rootItem->setProperty("themeIconActive", config.iconActiveColor);
    m_rootItem->setProperty("themeIconRecord", config.iconRecordColor);
    m_rootItem->setProperty("themeIconCancel", config.iconCancelColor);
    m_rootItem->setProperty("themeCornerRadius", config.cornerRadius);
}

void QmlRecordingControlBar::applyPlatformWindowFlags()
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

    // Raise above menu bar
    [window setLevel:kCGScreenSaverWindowLevel]; // level 1000

    // Keep visible when app deactivates (LSUIElement apps hide windows)
    [window setHidesOnDeactivate:NO];

    // Equivalent of WA_ShowWithoutActivating: prevent the panel from
    // becoming key window (and thus activating the app) unless a view
    // explicitly needs keyboard focus. Our control bar only uses
    // MouseAreas and the ESC shortcut is Qt::ApplicationShortcut, so
    // this is safe.
    if ([window isKindOfClass:[NSPanel class]]) {
        [(NSPanel*)window setBecomesKeyOnlyIfNeeded:YES];
    }

    // Remove resizable style mask to hide macOS resize grip
    NSUInteger mask = [window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [window setStyleMask:mask];

    // Do NOT call setIgnoresMouseEvents — this window needs mouse interaction
#endif
}

void QmlRecordingControlBar::show()
{
    ensureView();
    m_view->show();
    applyPlatformWindowFlags();
    m_view->raise();
}

void QmlRecordingControlBar::hide()
{
    if (m_view)
        m_view->hide();
}

void QmlRecordingControlBar::close()
{
    hideTooltip();

    if (m_view) {
        m_view->close();
        m_view->deleteLater();
        m_view = nullptr;
        m_rootItem = nullptr;
    }

    delete m_escShortcut;
    m_escShortcut = nullptr;
}

WId QmlRecordingControlBar::winId() const
{
    if (m_view)
        return m_view->winId();
    return 0;
}

void QmlRecordingControlBar::raiseAboveMenuBar()
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

void QmlRecordingControlBar::setExcludedFromCapture(bool excluded)
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
#elif defined(Q_OS_WIN)
    if (!m_view)
        return;

    HWND hwnd = reinterpret_cast<HWND>(m_view->winId());
    if (!hwnd)
        return;

    constexpr DWORD kExcludeFromCapture = 0x00000011;
    const DWORD affinity = excluded ? kExcludeFromCapture : WDA_NONE;
    SetWindowDisplayAffinity(hwnd, affinity);
#else
    Q_UNUSED(excluded)
#endif
}

// ── Positioning (exact replica of Widget version) ──

void QmlRecordingControlBar::positionNear(const QRect& recordingRegion)
{
    if (!m_view)
        return;

    m_recordingRegion = recordingRegion;

    QScreen* screen = QGuiApplication::screenAt(recordingRegion.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    const QRect screenGeom = screen->geometry();
    const int w = m_view->width();
    const int h = m_view->height();
    int x, y;

    const bool isFullScreen = (recordingRegion == screenGeom);

    // 8px visual gap + 4px border width + 3px glow effect = 15px
    const int kToolbarMargin = 15;

    if (isFullScreen) {
        x = screenGeom.center().x() - w / 2;
        y = screenGeom.bottom() - h - 60;
    } else {
        x = recordingRegion.center().x() - w / 2;
        y = recordingRegion.bottom() + kToolbarMargin;

        if (y + h > screenGeom.bottom() - 10)
            y = recordingRegion.top() - h - kToolbarMargin;

        if (y < screenGeom.top() + 10)
            y = recordingRegion.top() + 10;
    }

    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - w - 10);

    m_view->setPosition(x, y);
}

// ── Data update methods ──

void QmlRecordingControlBar::updateDuration(qint64 elapsedMs)
{
    if (m_rootItem)
        m_rootItem->setProperty("duration", formatDuration(elapsedMs));
}

QString QmlRecordingControlBar::formatDuration(qint64 ms) const
{
    int hours = ms / 3600000;
    int minutes = (ms % 3600000) / 60000;
    int seconds = (ms % 60000) / 1000;

    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void QmlRecordingControlBar::setPaused(bool paused)
{
    m_isPaused = paused;
    if (m_rootItem)
        m_rootItem->setProperty("isPaused", paused);
}

void QmlRecordingControlBar::setPreparing(bool preparing)
{
    if (!m_rootItem)
        return;

    m_rootItem->setProperty("isPreparing", preparing);
    if (preparing)
        m_rootItem->setProperty("duration", tr("Preparing..."));
    else
        m_rootItem->setProperty("duration", QStringLiteral("00:00:00"));

    // SizeViewToRootObject auto-resizes; onWidthChanged handles repositioning
}

void QmlRecordingControlBar::setPreparingStatus(const QString& status)
{
    if (m_rootItem)
        m_rootItem->setProperty("preparingStatus", status);

    // SizeViewToRootObject auto-resizes; onWidthChanged handles repositioning
}

void QmlRecordingControlBar::updateRegionSize(int width, int height)
{
    if (m_rootItem)
        m_rootItem->setProperty("regionSize",
            QStringLiteral("%1\u00D7%2").arg(width).arg(height));
}

void QmlRecordingControlBar::updateFps(double fps)
{
    if (m_rootItem)
        m_rootItem->setProperty("fpsText", tr("%1 fps").arg(qRound(fps)));
}

void QmlRecordingControlBar::setAudioEnabled(bool enabled)
{
    if (m_rootItem)
        m_rootItem->setProperty("audioEnabled", enabled);

    // SizeViewToRootObject auto-resizes; onWidthChanged handles repositioning
}

// ── Tooltip management ──

void QmlRecordingControlBar::onButtonHovered(int buttonId, qreal anchorX, qreal anchorY,
                                              qreal anchorW, qreal anchorH)
{
    QString tip = tooltipForButton(buttonId);
    if (tip.isEmpty()) {
        hideTooltip();
        return;
    }

    // Convert QML-local coordinates to global screen coordinates
    if (!m_view)
        return;

    QPoint globalPos(m_view->x() + qRound(anchorX),
                     m_view->y() + qRound(anchorY));
    QRect anchorRect(globalPos, QSize(qRound(anchorW), qRound(anchorH)));

    showTooltip(tip, anchorRect);
}

void QmlRecordingControlBar::onButtonUnhovered()
{
    hideTooltip();
}

QString QmlRecordingControlBar::tooltipForButton(int buttonId) const
{
    switch (buttonId) {
    case 0: // Pause
        return m_isPaused ? tr("Resume Recording") : tr("Pause Recording");
    case 1: // Stop
        return tr("Stop Recording");
    case 2: // Cancel
        return tr("Cancel Recording (Esc)");
    default:
        return QString();
    }
}

void QmlRecordingControlBar::showTooltip(const QString& text, const QRect& anchorRect)
{
    if (!m_tooltip) {
        m_tooltip = new GlassTooltip(nullptr);
        setWindowExcludedFromCapture(m_tooltip, true);
        connect(m_tooltip, &QObject::destroyed, this, [this]() {
            m_tooltip = nullptr;
        });
    }

    const auto& style = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    const QPoint anchorCenter = anchorRect.center();

    // Use the full control bar bounds as anchor (not the button bounds)
    // so the tooltip has enough visual gap from the bar edge.
    const int barBottom = m_view->y() + m_view->height();
    const int barTop = m_view->y();

    // Show tooltip below the control bar
    const QPoint anchorEdge(anchorCenter.x(), barBottom);
    m_tooltip->show(text, style, anchorEdge, /*above=*/false, /*showShadow=*/true);

    // Fallback: if tooltip goes off screen, flip to above the control bar
    QScreen* screen = QGuiApplication::screenAt(anchorCenter);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect bounds = screen->availableGeometry();
        const QRect tipGeom(m_tooltip->pos(), m_tooltip->size());
        if (tipGeom.bottom() > bounds.bottom() - 5) {
            const QPoint flippedEdge(anchorCenter.x(), barTop);
            m_tooltip->show(text, style, flippedEdge, /*above=*/true, /*showShadow=*/true);
        }
        // Final safety clamp
        const QRect finalGeom(m_tooltip->pos(), m_tooltip->size());
        if (finalGeom.bottom() > bounds.bottom() - 5)
            m_tooltip->move(m_tooltip->x(), bounds.bottom() - m_tooltip->height() - 5);
    }
}

void QmlRecordingControlBar::hideTooltip()
{
    if (m_tooltip)
        m_tooltip->hideTooltip();
}

// ── Drag handling ──

void QmlRecordingControlBar::onDragStarted()
{
    m_isDragging = true;
    if (m_view)
        m_dragStartViewPos = m_view->position();
    hideTooltip();
}

void QmlRecordingControlBar::onDragFinished()
{
    m_isDragging = false;
}

void QmlRecordingControlBar::onDragMoved(qreal deltaX, qreal deltaY)
{
    if (!m_view || !m_isDragging)
        return;

    QPoint newPos(m_dragStartViewPos.x() + qRound(deltaX),
                  m_dragStartViewPos.y() + qRound(deltaY));

    // Clamp to screen boundaries
    QScreen* screen = QGuiApplication::screenAt(newPos + QPoint(m_view->width() / 2, m_view->height() / 2));
    if (screen) {
        const QRect screenGeom = screen->geometry();
        newPos.setX(qBound(screenGeom.left(), newPos.x(), screenGeom.right() - m_view->width()));
        newPos.setY(qBound(screenGeom.top(), newPos.y(), screenGeom.bottom() - m_view->height()));
    }

    m_view->setPosition(newPos);
    hideTooltip();
}

void QmlRecordingControlBar::onWidthChanged()
{
    if (!m_view || !m_rootItem || m_isDragging)
        return;

    // SizeViewToRootObject auto-syncs view width to root item.
    // Re-center the window around the old center point.
    int viewWidth = m_view->width();
    int rootWidth = qRound(m_rootItem->width());

    // If SizeViewToRootObject hasn't caught up yet, nudge it
    if (viewWidth != rootWidth)
        viewWidth = rootWidth;

    QPoint center(m_view->x() + m_view->width() / 2, m_view->y());
    m_view->setPosition(center.x() - viewWidth / 2, m_view->y());
}

} // namespace SnapTray
