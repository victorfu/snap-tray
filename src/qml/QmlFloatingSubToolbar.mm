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
// The auto-blur button is also emphasized while its tooltip shows.
const QLatin1String kAutoBlurButtonName("autoBlurButton");

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
    hideControlTooltip();
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
                SIGNAL(controlTooltipRequested(QString,QString)),
                this,
                SLOT(onControlTooltipRequested(QString,QString)));
        connect(m_rootItem,
                SIGNAL(controlTooltipDismissed(QString)),
                this,
                SLOT(onControlTooltipDismissed(QString)));
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
    const bool wasVisible = isVisible();
    hideControlTooltip();
    if (m_view) {
        m_view->hide();
        m_view->unsetCursor();
    }
    CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
    // A paint can hide an already hidden sub-toolbar. Restoring the host cursor
    // sends a mouse move, which requests another paint; only do it on a real
    // visibility transition so the selector can become idle.
    if (wasVisible && m_parentWidget) {
        QTimer::singleShot(0, this, [this]() {
            CursorSurfaceSupport::restoreWidgetCursorIfPointerOver(m_parentWidget);
        });
    }
}

void QmlFloatingSubToolbar::close()
{
    hideControlTooltip();
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
            hideControlTooltip();
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
            m_view->unsetCursor();
            scheduleParentCursorRestore();
            break;
        case QEvent::Hide:
            hideControlTooltip();
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
            m_view->unsetCursor();
            scheduleParentCursorRestore();
            break;
        case QEvent::Close:
            hideControlTooltip();
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

void QmlFloatingSubToolbar::onControlTooltipRequested(const QString& controlName,
                                                      const QString& text)
{
    // A hidden control (say, the eyedropper while Mosaic shows) cannot take
    // the tooltip from the one showing it.
    QQuickItem* control = visibleControl(controlName);
    if (!control || text.isEmpty()) {
        return;
    }

    // One tooltip at a time: the latest control takes it over without a flicker.
    if (control != m_tooltipControl) {
        disconnect(m_tooltipControlVisibility);
        m_tooltipControl = control;
        // A tool or selection change can hide the control under a still
        // pointer, which sends no hover exit.
        m_tooltipControlVisibility = connect(
            control, &QQuickItem::visibleChanged, this, [this]() {
                if (!m_tooltipControl || !m_tooltipControl->isVisible()) {
                    hideControlTooltip();
                }
            });
    }
    m_tooltipText = text;
    showControlTooltip();
}

void QmlFloatingSubToolbar::onControlTooltipDismissed(const QString& controlName)
{
    if (m_tooltipControl && m_tooltipControl->objectName() == controlName) {
        hideControlTooltip();
    }
}

void QmlFloatingSubToolbar::showControlTooltip()
{
    if (!m_tooltipControl) {
        return;
    }

    const QRect anchorGlobalRect = controlGlobalRect(m_tooltipControl->objectName());
    if (!m_view || !m_view->isVisible() || anchorGlobalRect.isEmpty()) {
        hideControlTooltip();
        return;
    }

    // Keep the tooltip outside the whole strip while preserving the control's
    // horizontal center as its visual anchor.
    QRect placementAnchor = anchorGlobalRect;
    placementAnchor.setTop(qMin(placementAnchor.top(), m_view->y()));
    placementAnchor.setBottom(qMax(placementAnchor.bottom(),
                                   m_view->y() + m_view->height() - 1));

    setAutoBlurHintEmphasis(m_tooltipControl->objectName() == kAutoBlurButtonName);
    m_tooltip.showFor(m_tooltipText, placementAnchor, m_view, m_tooltipPlacement);
}

void QmlFloatingSubToolbar::hideControlTooltip()
{
    m_tooltip.hide();
    disconnect(m_tooltipControlVisibility);
    m_tooltipControl.clear();
    m_tooltipText.clear();
    setAutoBlurHintEmphasis(false);
}

void QmlFloatingSubToolbar::setAutoBlurHintEmphasis(bool active)
{
    if (m_rootItem) {
        m_rootItem->setProperty("autoBlurHintActive", active);
    }
}

QQuickItem* QmlFloatingSubToolbar::visibleControl(const QString& controlName) const
{
    if (!m_rootItem || controlName.isEmpty()) {
        return nullptr;
    }

    auto* control = m_rootItem->findChild<QQuickItem*>(controlName);
    return control && control->isVisible() ? control : nullptr;
}

QRect QmlFloatingSubToolbar::controlGlobalRect(const QString& controlName) const
{
    const QQuickItem* control = visibleControl(controlName);
    if (!control) {
        return {};
    }

    const QPointF topLeft = control->mapToGlobal(QPointF(0.0, 0.0));
    return QRect(qRound(topLeft.x()),
                 qRound(topLeft.y()),
                 qMax(1, qRound(control->width())),
                 qMax(1, qRound(control->height())));
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

    // Re-anchor a showing tooltip to the moved strip.
    showControlTooltip();
}

} // namespace SnapTray
