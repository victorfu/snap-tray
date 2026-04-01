#include "qml/QmlFloatingSubToolbar.h"
#include "cursor/CursorSurfaceSupport.h"
#include "platform/WindowLevel.h"
#include "qml/QmlOverlayManager.h"
#include "qml/PinToolOptionsViewModel.h"

#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QCursor>
#include <QTimer>
#include <QWidget>
#include <QVariant>
#include <QtMath>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace SnapTray {

namespace {
constexpr int kParentCursorRestoreRetryDelayMs = 16;
constexpr int kParentCursorRestoreMaxAttempts = 6;

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

QmlFloatingSubToolbar::QmlFloatingSubToolbar(PinToolOptionsViewModel* viewModel,
                                             QObject* parent)
    : QObject(parent)
    , m_viewModel(viewModel)
{
    connect(m_viewModel, &PinToolOptionsViewModel::emojiPickerRequested,
            this, &QmlFloatingSubToolbar::emojiPickerRequested);
}

QmlFloatingSubToolbar::QmlFloatingSubToolbar(QObject* parent)
    : QObject(parent)
    , m_viewModel(new PinToolOptionsViewModel(this))
{
    connect(m_viewModel, &PinToolOptionsViewModel::emojiPickerRequested,
            this, &QmlFloatingSubToolbar::emojiPickerRequested);
}

QmlFloatingSubToolbar::~QmlFloatingSubToolbar()
{
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->removeEventFilter(this);
    }
    destroyQuickView(m_view, m_rootItem);
}

void QmlFloatingSubToolbar::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay();
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_view->rootContext()->setContextProperty(
        QStringLiteral("pinToolOptionsViewModel"), m_viewModel);
    m_view->setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolOptionsStrip.qml")));

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "QmlFloatingSubToolbar QML error:" << error.toString();
    }

    m_rootItem = m_view->rootObject();
    if (m_rootItem) {
        m_rootItem->setProperty(
            "viewModel",
            QVariant::fromValue(static_cast<QObject*>(m_viewModel)));
    }

    m_view->installEventFilter(this);
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("QmlFloatingSubToolbar"));
    m_cursorOwnerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("QmlFloatingSubToolbar"));
}

void QmlFloatingSubToolbar::applyPlatformWindowFlags()
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

    // Set sub-toolbar one level above parent so it stays visible over fullscreen widgets
    NSInteger targetLevel = NSFloatingWindowLevel;
    if (m_parentWidget) {
        NSView* parentView = reinterpret_cast<NSView*>(m_parentWidget->winId());
        if (parentView) {
            NSWindow* parentWindow = [parentView window];
            if (parentWindow) {
                targetLevel = qMax(targetLevel, [parentWindow level] + 1);
            }
        }
    }
    [window setLevel:targetLevel];
    [window setHidesOnDeactivate:NO];
    [window setSharingType:NSWindowSharingNone];

    if ([window isKindOfClass:[NSPanel class]]) {
        [(NSPanel*)window setBecomesKeyOnlyIfNeeded:YES];
    }

    NSUInteger mask = [window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [window setStyleMask:mask];
#endif

    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
}

// ── Show / Hide / Close ──

void QmlFloatingSubToolbar::show()
{
    ensureView();

    if (!m_rootItem)
        return;

    syncTransientParent();
    m_view->show();
    applyPlatformWindowFlags();
    QmlOverlayManager::enableNativeShadow(m_view);
    m_view->raise();
    syncCursorSurface();
}

void QmlFloatingSubToolbar::hide()
{
    if (m_view) {
        m_view->hide();
        m_view->unsetCursor();
    }
    CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
    if (m_parentWidget) {
        QTimer::singleShot(0, this, [this]() {
            CursorSurfaceSupport::restoreWidgetCursorIfPointerOver(m_parentWidget);
        });
    }
}

void QmlFloatingSubToolbar::close()
{
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->removeEventFilter(this);
        m_view->unsetCursor();
    }
    if (m_parentWidget) {
        QTimer::singleShot(0, this, [this]() {
            CursorSurfaceSupport::restoreWidgetCursorIfPointerOver(m_parentWidget);
        });
    }
    destroyQuickView(m_view, m_rootItem);
}

bool QmlFloatingSubToolbar::isVisible() const
{
    return m_view && m_view->isVisible();
}

QRect QmlFloatingSubToolbar::geometry() const
{
    if (!m_view)
        return QRect();
    return QRect(m_view->position(), m_view->size());
}

QWindow* QmlFloatingSubToolbar::window() const
{
    return m_view;
}

PinToolOptionsViewModel* QmlFloatingSubToolbar::viewModel() const
{
    return m_viewModel;
}

void QmlFloatingSubToolbar::setParentWidget(QWidget* parent)
{
    m_parentWidget = parent;
    syncTransientParent();
}

void QmlFloatingSubToolbar::syncTransientParent()
{
    if (!m_view) {
        return;
    }

    QWidget* hostWindow = m_parentWidget ? m_parentWidget->window() : nullptr;
    if (hostWindow && hostWindow->windowHandle()) {
        m_view->setTransientParent(hostWindow->windowHandle());
    } else {
        m_view->setTransientParent(nullptr);
    }

    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
    if (m_view->isVisible()) {
        applyPlatformWindowFlags();
    }
}

void QmlFloatingSubToolbar::syncCursorSurface()
{
    if (!m_view || m_cursorSurfaceId.isEmpty() || m_cursorOwnerId.isEmpty()) {
        return;
    }

    if (!m_view->isVisible()) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->unsetCursor();
        return;
    }

    const QRect bounds(m_view->position(), m_view->size());
    if (!bounds.contains(QCursor::pos())) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->unsetCursor();
        return;
    }

    cancelParentCursorRestore();

    CursorSurfaceSupport::syncWindowSurface(
        m_view, m_cursorSurfaceId, m_cursorOwnerId, CursorRequestSource::Overlay);
}

void QmlFloatingSubToolbar::cancelParentCursorRestore()
{
    ++m_parentCursorRestoreToken;
}

void QmlFloatingSubToolbar::scheduleParentCursorRestore()
{
    if (!m_parentWidget) {
        return;
    }

    const quint64 token = ++m_parentCursorRestoreToken;
    QTimer::singleShot(0, this, [this, token]() {
        attemptParentCursorRestore(token, kParentCursorRestoreMaxAttempts);
    });
}

void QmlFloatingSubToolbar::attemptParentCursorRestore(quint64 token, int remainingAttempts)
{
    if (token != m_parentCursorRestoreToken || !m_parentWidget) {
        return;
    }

    const bool pointerStillWithinSubToolbar =
        m_view &&
        m_view->isVisible() &&
        QRect(m_view->position(), m_view->size()).contains(QCursor::pos());
    if (pointerStillWithinSubToolbar) {
        if (remainingAttempts > 0) {
            QTimer::singleShot(kParentCursorRestoreRetryDelayMs, this, [this, token, remainingAttempts]() {
                attemptParentCursorRestore(token, remainingAttempts - 1);
            });
        }
        return;
    }

    CursorSurfaceSupport::restoreWidgetCursorIfPointerOver(m_parentWidget);
}

bool QmlFloatingSubToolbar::eventFilter(QObject* obj, QEvent* event)
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
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
            m_view->unsetCursor();
            scheduleParentCursorRestore();
            break;
        default:
            break;
        }
    }

    return QObject::eventFilter(obj, event);
}

// ── Tool display ──

void QmlFloatingSubToolbar::showForTool(int toolId)
{
    m_viewModel->showForTool(toolId);

    if (m_viewModel->hasContent()) {
        show();
    } else {
        hide();
    }
}

// ── Positioning ──

void QmlFloatingSubToolbar::positionBelow(const QRect& toolbarRect)
{
    ensureView();
    if (!m_view)
        return;

    QScreen* screen = QGuiApplication::screenAt(toolbarRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    const QRect screenGeom = screen->geometry();
    const int w = m_view->width();
    const int h = m_view->height();
    constexpr int kMargin = 4;

    int x = toolbarRect.left();
    int y = toolbarRect.bottom() + kMargin;

    if (y + h > screenGeom.bottom() - 10)
        y = toolbarRect.top() - h - kMargin;

    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - w - 10);

    const QPoint targetPos(x, y);
    if (m_view->position() != targetPos) {
        m_view->setPosition(targetPos);
    }
    syncCursorSurface();
}

} // namespace SnapTray
