#include "qml/QmlWindowedToolbar.h"
#include "cursor/CursorSurfaceSupport.h"
#include "platform/WindowLevel.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlOverlayManager.h"
#include "qml/PinToolbarViewModel.h"

#include <QCoreApplication>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QCursor>
#include <QWidget>
#include <QWindow>
#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <QVariant>
#include <QtMath>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace SnapTray {

namespace {
bool containsGlobalPoint(const QWidget* widget, const QPoint& globalPos)
{
    return widget && widget->isVisible() && widget->frameGeometry().contains(globalPos);
}

bool containsGlobalPoint(const QWindow* window, const QPoint& globalPos)
{
    if (!window || !window->isVisible()) {
        return false;
    }

    const QRect frameRect = window->frameGeometry();
    if (frameRect.isValid() && !frameRect.isEmpty()) {
        return frameRect.contains(globalPos);
    }

    return QRect(window->position(), window->size()).contains(globalPos);
}

bool isTransientPopup(const QWidget* widget)
{
    return widget &&
           (qobject_cast<const QDialog*>(widget) ||
            qobject_cast<const QMenu*>(widget) ||
            widget->windowFlags().testFlag(Qt::Popup));
}

void destroyQuickView(QQuickView*& view, QQuickItem*& rootItem)
{
    if (!view)
        return;

    view->close();
    delete view;
    view = nullptr;
    rootItem = nullptr;
}

} // namespace

QmlWindowedToolbar::QmlWindowedToolbar(QObject* parent)
    : QObject(parent)
    , m_viewModel(new PinToolbarViewModel(this))
{
}

QmlWindowedToolbar::~QmlWindowedToolbar()
{
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->removeEventFilter(this);
        qApp->removeEventFilter(this);
    }
    destroyQuickView(m_view, m_rootItem);
}

void QmlWindowedToolbar::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay();
    // This floating toolbar must stay mouse-interactive without stealing
    // keyboard focus from the host PinWindow.
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    const QVariantMap initialProperties{
        {QStringLiteral("viewModel"),
         QVariant::fromValue(static_cast<QObject*>(m_viewModel))},
    };
    m_view->setInitialProperties(initialProperties);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/FloatingToolbar.qml")));

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "FloatingToolbar QML error:" << error.toString();
    }

    m_rootItem = m_view->rootObject();

    m_view->installEventFilter(this);
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("QmlWindowedToolbar"));
    m_cursorOwnerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("QmlWindowedToolbar"));

    if (m_rootItem)
        setupConnections();
}

void QmlWindowedToolbar::setupConnections()
{
    if (!m_rootItem)
        return;

    auto connectOrWarn = [this](const char* signal, const char* slot, const char* description) {
        if (!connect(m_rootItem, signal, this, slot))
            qWarning() << "QmlWindowedToolbar: failed to connect" << description;
    };

    // Tooltip signals
    connectOrWarn(SIGNAL(buttonHovered(int,double,double,double,double)),
                  SLOT(onButtonHovered(int,double,double,double,double)),
                  "buttonHovered");
    connectOrWarn(SIGNAL(buttonUnhovered()),
                  SLOT(onButtonUnhovered()),
                  "buttonUnhovered");

    // Drag signals
    connectOrWarn(SIGNAL(dragStarted()), SLOT(onDragStarted()), "dragStarted");
    connectOrWarn(SIGNAL(dragFinished()), SLOT(onDragFinished()), "dragFinished");
    connectOrWarn(SIGNAL(dragMoved(double,double)),
                  SLOT(onDragMoved(double,double)),
                  "dragMoved");
}

void QmlWindowedToolbar::applyPlatformWindowFlags()
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

    // Raise above other windows but below screen saver level
    [window setLevel:NSFloatingWindowLevel];

    // Keep visible when app deactivates
    [window setHidesOnDeactivate:NO];

    // Prevent becoming key window unless needed
    if ([window isKindOfClass:[NSPanel class]]) {
        [(NSPanel*)window setBecomesKeyOnlyIfNeeded:YES];
    }

    // Remove resizable style mask
    NSUInteger mask = [window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [window setStyleMask:mask];
#endif

    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
}

// ── Show / Hide / Close ──

void QmlWindowedToolbar::show()
{
    ensureView();

    if (!m_rootItem) {
        // QML failed to load — skip showing and click-outside detection
        return;
    }

    syncTransientParent();
    m_view->show();
    applyPlatformWindowFlags();
    QmlOverlayManager::enableNativeShadow(m_view);
    m_view->raise();

    // Install event filter for click-outside detection
    qApp->installEventFilter(this);
    m_showTime.start();
    syncCursorSurface();
}

void QmlWindowedToolbar::hide()
{
    m_tooltip.hide();
    if (m_view) {
        m_view->hide();
        qApp->removeEventFilter(this);
    }
    CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
    if (m_associatedPinWindow) {
        QTimer::singleShot(0, this, [this]() {
            CursorSurfaceSupport::restoreWidgetCursorIfPointerOver(m_associatedPinWindow);
        });
    }
}

void QmlWindowedToolbar::close()
{
    m_tooltip.hide();

    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->removeEventFilter(this);
        qApp->removeEventFilter(this);
    }
    if (m_associatedPinWindow) {
        QTimer::singleShot(0, this, [this]() {
            CursorSurfaceSupport::restoreWidgetCursorIfPointerOver(m_associatedPinWindow);
        });
    }
    destroyQuickView(m_view, m_rootItem);
}

bool QmlWindowedToolbar::isVisible() const
{
    return m_view && m_view->isVisible();
}

QRect QmlWindowedToolbar::geometry() const
{
    if (!m_view)
        return QRect();
    return QRect(m_view->position(), m_view->size());
}

QWindow* QmlWindowedToolbar::window() const
{
    return m_view;
}

QWindow* QmlWindowedToolbar::tooltipWindow() const
{
    return m_tooltip.window();
}

PinToolbarViewModel* QmlWindowedToolbar::viewModel() const
{
    return m_viewModel;
}

void QmlWindowedToolbar::setAssociatedWidgets(QWidget* pinWindow, QmlFloatingSubToolbar* subToolbar)
{
    m_associatedPinWindow = pinWindow;
    m_associatedSubToolbar = subToolbar;
    syncTransientParent();
}

void QmlWindowedToolbar::setAssociatedTransientWidget(QWidget* widget)
{
    m_associatedTransientWidget = widget;
}

void QmlWindowedToolbar::setAssociatedTransientWindow(QWindow* window)
{
    m_associatedTransientWindow = window;
}

void QmlWindowedToolbar::syncTransientParent()
{
    if (!m_view) {
        return;
    }

    QWidget* hostWindow = m_associatedPinWindow ? m_associatedPinWindow->window() : nullptr;
#ifdef Q_OS_LINUX
    if (hostWindow && hostWindow->windowFlags().testFlag(Qt::X11BypassWindowManagerHint)) {
        m_view->setTransientParent(nullptr);
        QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
        if (m_view->isVisible()) {
            applyPlatformWindowFlags();
        }
        return;
    }
#endif
    if (hostWindow && hostWindow->windowHandle()) {
        m_view->setTransientParent(hostWindow->windowHandle());
    } else {
        m_view->setTransientParent(nullptr);
    }

    // Owner/transient changes on Windows can reintroduce native caption bits.
    // Reapply frameless tool-window styles after every parent sync.
    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
    if (m_view->isVisible()) {
        applyPlatformWindowFlags();
    }
}

// ── Positioning ──

void QmlWindowedToolbar::positionNear(const QRect& pinWindowRect)
{
    if (!m_view)
        return;

    QScreen* screen = QGuiApplication::screenAt(pinWindowRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    const QRect screenGeom = screen->geometry();
    const int w = m_view->width();
    const int h = m_view->height();
    constexpr int kMargin = 8;

    // Position below pin window, right-aligned (same as WindowedToolbar)
    int x = pinWindowRect.right() - w + 1;
    int y = pinWindowRect.bottom() + kMargin;

    // If toolbar would go off bottom, position above
    if (y + h > screenGeom.bottom() - 10)
        y = pinWindowRect.top() - h - kMargin;

    // If still off screen, position inside at bottom
    if (y < screenGeom.top() + 10)
        y = pinWindowRect.bottom() - h - 10;

    // Keep on screen horizontally
    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - w - 10);

    m_view->setPosition(x, y);
    syncCursorSurface();
}

void QmlWindowedToolbar::syncCursorSurface(const CursorStyleSpec* explicitStyle)
{
    if (!m_view || m_cursorSurfaceId.isEmpty() || m_cursorOwnerId.isEmpty()) {
        return;
    }

    if (!m_view->isVisible()) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        return;
    }

    const QRect bounds(m_view->position(), m_view->size());
    if (!bounds.contains(QCursor::pos())) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        return;
    }

    const CursorStyleSpec dragStyle = CursorStyleSpec::fromShape(Qt::ClosedHandCursor);
    const CursorStyleSpec* resolvedStyle = explicitStyle ? explicitStyle
                                                         : (m_isDragging ? &dragStyle : nullptr);
    CursorSurfaceSupport::syncWindowSurface(
        m_view, m_cursorSurfaceId, m_cursorOwnerId, CursorRequestSource::Overlay, resolvedStyle);
}

// ── Click-outside detection ──

bool QmlWindowedToolbar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_view) {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::Show:
            syncCursorSurface();
            break;
        case QEvent::Leave:
        case QEvent::Hide:
        case QEvent::Close:
            m_tooltip.hide();
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
            if (m_associatedPinWindow) {
                QTimer::singleShot(0, this, [this]() {
                    CursorSurfaceSupport::restoreWidgetCursorIfPointerOver(m_associatedPinWindow);
                });
            }
            break;
        default:
            break;
        }
        return false;
    }

    // Ignore during first 300ms to prevent accidental close
    if (m_showTime.isValid() && m_showTime.elapsed() < 300)
        return false;

    if (event->type() != QEvent::MouseButtonPress)
        return false;

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    QPoint globalPos = mouseEvent->globalPosition().toPoint();

    // Check if click is inside the toolbar itself
    if (m_view) {
        QRect viewRect(m_view->position(), m_view->size());
        if (viewRect.contains(globalPos))
            return false;
    }

    // Check if click is inside the associated pin window
    if (m_associatedPinWindow && m_associatedPinWindow->isVisible()) {
        QRect pinRect = m_associatedPinWindow->frameGeometry();
        if (pinRect.contains(globalPos))
            return false;
    }

    // Check if click is inside the sub-toolbar (QML-based)
    if (m_associatedSubToolbar && m_associatedSubToolbar->isVisible()) {
        QRect subRect = m_associatedSubToolbar->geometry();
        if (subRect.contains(globalPos))
            return false;
    }

    // Check if click is inside an explicitly associated transient widget
    if (containsGlobalPoint(m_associatedTransientWidget.data(), globalPos))
        return false;

    if (containsGlobalPoint(m_associatedTransientWindow.data(), globalPos))
        return false;

    // Don't close if click is inside an active popup (QMenu, dropdown, etc.)
    if (QWidget* popup = QApplication::activePopupWidget()) {
        if (containsGlobalPoint(popup, globalPos))
            return false;
    }

    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (const QWidget* widget : topLevelWidgets) {
        if (!isTransientPopup(widget))
            continue;
        if (containsGlobalPoint(widget, globalPos))
            return false;
    }

    // Click is outside — request close
    m_tooltip.hide();
    emit closeRequested();
    return false;  // Let the event propagate
}

// ── Tooltip management ──

void QmlWindowedToolbar::onButtonHovered(int buttonId, double anchorX, double anchorY,
                                         double anchorW, double anchorH)
{
    if (!m_view)
        return;

    syncCursorSurface();

    // Find the tooltip text from the ViewModel's button list
    QString tip;
    for (const auto& btn : m_viewModel->buttons()) {
        QVariantMap map = btn.toMap();
        if (map["id"].toInt() == buttonId) {
            tip = map["tooltip"].toString();
            break;
        }
    }

    if (tip.isEmpty()) {
        m_tooltip.hide();
        return;
    }

    QPoint globalPos(m_view->x() + qRound(anchorX),
                     m_view->y() + qRound(anchorY));
    QRect anchorRect(globalPos, QSize(qRound(anchorW), qRound(anchorH)));

    m_tooltip.setAssociatedWidget(m_associatedPinWindow);
    m_tooltip.showFor(tip, anchorRect, m_view, TooltipPlacement::Above);
}

void QmlWindowedToolbar::onButtonUnhovered()
{
    m_tooltip.hide();
}

// ── Drag handling ──

void QmlWindowedToolbar::onDragStarted()
{
    m_isDragging = true;
    if (m_view)
        m_dragStartViewPos = m_view->position();
    m_dragStartCursorPos = QCursor::pos();
    m_tooltip.hide();
    syncCursorSurface();
}

void QmlWindowedToolbar::onDragFinished()
{
    m_isDragging = false;
    QTimer::singleShot(0, this, [this]() {
        const CursorStyleSpec idleStyle = CursorStyleSpec::fromShape(Qt::ArrowCursor);
        syncCursorSurface(&idleStyle);
        if (m_associatedPinWindow) {
            CursorSurfaceSupport::restoreWidgetCursorIfPointerOver(m_associatedPinWindow);
        }
    });
}

void QmlWindowedToolbar::onDragMoved(double deltaX, double deltaY)
{
    Q_UNUSED(deltaX)
    Q_UNUSED(deltaY)

    if (!m_view || !m_isDragging)
        return;

    QPoint newPos = m_dragStartViewPos + (QCursor::pos() - m_dragStartCursorPos);

    // Clamp to screen boundaries
    QScreen* screen = QGuiApplication::screenAt(
        newPos + QPoint(m_view->width() / 2, m_view->height() / 2));
    if (screen) {
        const QRect screenGeom = screen->geometry();
        newPos.setX(qBound(screenGeom.left(), newPos.x(), screenGeom.right() - m_view->width()));
        newPos.setY(qBound(screenGeom.top(), newPos.y(), screenGeom.bottom() - m_view->height()));
    }

    m_view->setPosition(newPos);
    syncCursorSurface();
    m_tooltip.hide();
}

} // namespace SnapTray
