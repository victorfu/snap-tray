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

QSize resolvedViewSize(QQuickView* view, QQuickItem* rootItem)
{
    if (!view) {
        return {};
    }

    QSize size = view->size();
    if (rootItem) {
        const qreal rootWidth = rootItem->implicitWidth() > 0.0
            ? rootItem->implicitWidth()
            : rootItem->width();
        const qreal rootHeight = rootItem->implicitHeight() > 0.0
            ? rootItem->implicitHeight()
            : rootItem->height();
        QSize rootSize(
            qRound(rootWidth),
            qRound(rootHeight));
        if (!rootSize.isEmpty()) {
            size = rootSize;
            if (view->size() != rootSize) {
                view->resize(rootSize);
            }
        }
    }
    return size;
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

QmlFloatingSubToolbar::QmlFloatingSubToolbar(PinToolOptionsViewModel* viewModel,
                                             QObject* parent)
    : QObject(parent)
    , m_viewModel(viewModel)
{
    connect(m_viewModel, &PinToolOptionsViewModel::emojiPickerRequested,
            this, &QmlFloatingSubToolbar::emojiPickerRequested);
    connect(m_viewModel, &PinToolOptionsViewModel::activeToolChanged,
            this, &QmlFloatingSubToolbar::onActiveToolChanged);
}

QmlFloatingSubToolbar::QmlFloatingSubToolbar(QObject* parent)
    : QObject(parent)
    , m_viewModel(new PinToolOptionsViewModel(this))
{
    connect(m_viewModel, &PinToolOptionsViewModel::emojiPickerRequested,
            this, &QmlFloatingSubToolbar::emojiPickerRequested);
    connect(m_viewModel, &PinToolOptionsViewModel::activeToolChanged,
            this, &QmlFloatingSubToolbar::onActiveToolChanged);
}

QmlFloatingSubToolbar::~QmlFloatingSubToolbar()
{
    hideAutoBlurHint();
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
        m_rootItem->setProperty("autoBlurHintActive", false);
        connect(m_rootItem,
                SIGNAL(autoBlurButtonHovered(double,double,double,double)),
                this,
                SLOT(onAutoBlurButtonHovered(double,double,double,double)));
        connect(m_rootItem,
                SIGNAL(autoBlurButtonHoverExited()),
                this,
                SLOT(onAutoBlurButtonHoverExited()));
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
    hideAutoBlurHint();
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
    hideAutoBlurHint();
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
    m_tooltip.setAssociatedWidget(parent);
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
            hideAutoBlurHint();
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
            m_view->unsetCursor();
            scheduleParentCursorRestore();
            break;
        case QEvent::Hide:
            hideAutoBlurHint();
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
            m_view->unsetCursor();
            scheduleParentCursorRestore();
            break;
        case QEvent::Close:
            hideAutoBlurHint();
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

    if (!m_viewModel->showAutoBlurSection()) {
        hideAutoBlurHint();
    }

    if (m_viewModel->hasContent()) {
        show();
    } else {
        hide();
    }
}

void QmlFloatingSubToolbar::onActiveToolChanged()
{
    if (!m_viewModel->isMosaicActive()) {
        hideAutoBlurHint();
    }
}

void QmlFloatingSubToolbar::onAutoBlurButtonHovered(double globalX,
                                                    double globalY,
                                                    double width,
                                                    double height)
{
    if (!m_viewModel->showAutoBlurSection()) {
        return;
    }

    const QRect anchor(QPoint(qRound(globalX), qRound(globalY)),
                       QSize(qMax(1, qRound(width)), qMax(1, qRound(height))));
    showAutoBlurHint(anchor);
}

void QmlFloatingSubToolbar::onAutoBlurButtonHoverExited()
{
    hideAutoBlurHint();
}

void QmlFloatingSubToolbar::showAutoBlurHint(const QRect& anchorGlobalRect)
{
    if (!m_view || !m_view->isVisible() || anchorGlobalRect.isEmpty()) {
        return;
    }

    const QString text = m_viewModel->autoBlurHintText();
    if (text.isEmpty()) {
        hideAutoBlurHint();
        return;
    }

    // Keep the tooltip outside the whole strip while preserving the button's
    // horizontal center as its visual anchor.
    QRect placementAnchor = anchorGlobalRect;
    placementAnchor.setTop(qMin(placementAnchor.top(), m_view->y()));
    placementAnchor.setBottom(qMax(placementAnchor.bottom(),
                                   m_view->y() + m_view->height() - 1));

    m_autoBlurHintVisible = true;
    setAutoBlurHintEmphasis(true);
    m_tooltip.showFor(text, placementAnchor, m_view, m_tooltipPlacement);
}

void QmlFloatingSubToolbar::hideAutoBlurHint()
{
    m_tooltip.hide();
    m_autoBlurHintVisible = false;
    setAutoBlurHintEmphasis(false);
}

void QmlFloatingSubToolbar::setAutoBlurHintEmphasis(bool active)
{
    if (m_rootItem) {
        m_rootItem->setProperty("autoBlurHintActive", active);
    }
}

QRect QmlFloatingSubToolbar::autoBlurButtonGlobalRect() const
{
    if (!m_rootItem) {
        return {};
    }

    auto* button = m_rootItem->findChild<QQuickItem*>(
        QStringLiteral("autoBlurButton"));
    if (!button || !button->isVisible()) {
        return {};
    }

    const QPointF topLeft = button->mapToGlobal(QPointF(0.0, 0.0));
    return QRect(qRound(topLeft.x()),
                 qRound(topLeft.y()),
                 qMax(1, qRound(button->width())),
                 qMax(1, qRound(button->height())));
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

    const QRect screenGeom = screen->availableGeometry().isValid()
        ? screen->availableGeometry()
        : screen->geometry();
    const QSize viewSize = resolvedViewSize(m_view, m_rootItem);
    const int w = viewSize.width();
    const int h = viewSize.height();
    constexpr int kMargin = 4;
    constexpr int kScreenInset = 10;

    int x = toolbarRect.left();
    int y = toolbarRect.bottom() + kMargin;

    if (y + h > screenGeom.bottom() - kScreenInset)
        y = toolbarRect.top() - h - kMargin;

    const int minX = screenGeom.left() + kScreenInset;
    const int minY = screenGeom.top() + kScreenInset;
    const int maxX = screenGeom.right() - w - kScreenInset;
    const int maxY = screenGeom.bottom() - h - kScreenInset;
    x = maxX < minX ? minX : qBound(minX, x, maxX);
    y = maxY < minY ? minY : qBound(minY, y, maxY);

    const QPoint targetPos(x, y);
    if (m_view->position() != targetPos) {
        m_view->setPosition(targetPos);
    }
    m_tooltipPlacement = targetPos.y() > toolbarRect.center().y()
        ? TooltipPlacement::Below
        : TooltipPlacement::Above;
    syncCursorSurface();

    if (m_autoBlurHintVisible) {
        const QRect anchor = autoBlurButtonGlobalRect();
        if (!anchor.isEmpty()) {
            showAutoBlurHint(anchor);
        }
    }
}

} // namespace SnapTray
