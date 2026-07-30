#include "qml/QmlFloatingSubToolbar.h"
#include "cursor/CursorSurfaceSupport.h"
#include "platform/WindowLevel.h"
#include "qml/QmlOverlayManager.h"
#include "qml/PinToolOptionsViewModel.h"
#include "settings/AnnotationSettingsManager.h"

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
constexpr int kMosaicCoachmarkDurationMs = 3000;

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
    initializeHintConnections();
}

QmlFloatingSubToolbar::QmlFloatingSubToolbar(QObject* parent)
    : QObject(parent)
    , m_viewModel(new PinToolOptionsViewModel(this))
{
    connect(m_viewModel, &PinToolOptionsViewModel::emojiPickerRequested,
            this, &QmlFloatingSubToolbar::emojiPickerRequested);
    initializeHintConnections();
}

QmlFloatingSubToolbar::~QmlFloatingSubToolbar()
{
    deactivateMosaicHint();
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->removeEventFilter(this);
    }
    destroyQuickView(m_view, m_rootItem);
}

void QmlFloatingSubToolbar::initializeHintConnections()
{
    m_mosaicCoachmarkTimer.setSingleShot(true);
    m_mosaicCoachmarkTimer.setInterval(kMosaicCoachmarkDurationMs);
    connect(&m_mosaicCoachmarkTimer, &QTimer::timeout, this, [this]() {
        if (m_mosaicHintDisplay == MosaicHintDisplay::Coachmark) {
            hideMosaicHint();
        }
    });

    connect(m_viewModel, &PinToolOptionsViewModel::activeToolChanged,
            this, &QmlFloatingSubToolbar::onActiveToolChanged);
    connect(m_viewModel, &PinToolOptionsViewModel::mosaicBrushAdjustmentLearned,
            this, &QmlFloatingSubToolbar::onMosaicBrushAdjustmentLearned);
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
        m_rootItem->setProperty("mosaicBrushHintActive", false);
        connect(m_rootItem,
                SIGNAL(mosaicBrushPreviewHovered(double,double,double,double)),
                this,
                SLOT(onMosaicBrushPreviewHovered(double,double,double,double)));
        connect(m_rootItem,
                SIGNAL(mosaicBrushPreviewHoverExited()),
                this,
                SLOT(onMosaicBrushPreviewHoverExited()));
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
    // Visibility can be suppressed temporarily while the active tool is kept
    // (for example, during a Region Capture selection drag). Preserve the
    // activation so reopening the same tool does not replay its coachmark.
    suspendMosaicHint();
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
    deactivateMosaicHint();
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
#ifdef Q_OS_LINUX
    // A transient child of an X11Bypass host can redirect focus back to the
    // overlay surface and break annotation shortcuts. Keep the sub-toolbar
    // WM-managed while retaining m_parentWidget for cursor restoration.
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
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
            m_view->unsetCursor();
            scheduleParentCursorRestore();
            break;
        case QEvent::Hide:
            suspendMosaicHint();
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
            m_view->unsetCursor();
            scheduleParentCursorRestore();
            break;
        case QEvent::Close:
            deactivateMosaicHint();
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

    if (m_viewModel->isMosaicActive()) {
        // activeToolChanged handles genuine tool transitions. This branch also
        // covers reopening a hidden overlay whose ViewModel still says Mosaic.
        if (!m_mosaicHintActivationActive) {
            activateMosaicHint();
        }
    } else if (m_mosaicHintActivationActive) {
        deactivateMosaicHint();
    }

    if (m_viewModel->hasContent()) {
        show();
    } else {
        hide();
    }
}

void QmlFloatingSubToolbar::onActiveToolChanged()
{
    if (m_viewModel->isMosaicActive()) {
        if (!m_mosaicHintActivationActive) {
            activateMosaicHint();
        }
        return;
    }

    deactivateMosaicHint();
}

void QmlFloatingSubToolbar::activateMosaicHint()
{
    m_mosaicHintActivationActive = true;
    m_mosaicPreviewHovered = false;
    m_suppressHoverReminderUntilExit = false;
    m_mosaicBrushAdjustmentLearned = AnnotationSettingsManager::instance()
        .loadMosaicBrushAdjustmentLearned();
    m_mosaicCoachmarkPending = !m_mosaicBrushAdjustmentLearned;
    m_mosaicCoachmarkTimer.stop();
    hideMosaicHint();
}

void QmlFloatingSubToolbar::deactivateMosaicHint()
{
    m_mosaicCoachmarkTimer.stop();
    m_mosaicCoachmarkPending = false;
    m_mosaicHintActivationActive = false;
    m_mosaicPreviewHovered = false;
    m_suppressHoverReminderUntilExit = false;
    hideMosaicHint();
}

void QmlFloatingSubToolbar::suspendMosaicHint()
{
    m_mosaicCoachmarkTimer.stop();
    m_mosaicPreviewHovered = false;
    m_suppressHoverReminderUntilExit = false;
    hideMosaicHint();
}

void QmlFloatingSubToolbar::onMosaicBrushAdjustmentLearned()
{
    if (!m_mosaicHintActivationActive || !m_viewModel->isMosaicActive()) {
        return;
    }

    if (!m_mosaicBrushAdjustmentLearned) {
        AnnotationSettingsManager::instance()
            .saveMosaicBrushAdjustmentLearned(true);
        m_mosaicBrushAdjustmentLearned = true;
    }

    m_mosaicCoachmarkPending = false;
    m_mosaicCoachmarkTimer.stop();
    // The wheel gesture normally occurs while the preview is hovered. Keep the
    // newly learned reminder dismissed until the pointer leaves and re-enters.
    m_suppressHoverReminderUntilExit = m_mosaicPreviewHovered;
    hideMosaicHint();
}

void QmlFloatingSubToolbar::onMosaicBrushPreviewHovered(double globalX,
                                                        double globalY,
                                                        double width,
                                                        double height)
{
    m_mosaicPreviewHovered = true;
    if (!m_mosaicHintActivationActive || !m_viewModel->isMosaicActive()
        || m_suppressHoverReminderUntilExit) {
        return;
    }

    const QRect anchor(QPoint(qRound(globalX), qRound(globalY)),
                       QSize(qMax(1, qRound(width)), qMax(1, qRound(height))));

    if (m_mosaicHintDisplay == MosaicHintDisplay::Coachmark) {
        showMosaicHint(MosaicHintDisplay::Coachmark, anchor);
        return;
    }

    // A pending coachmark is deliberately left for positionBelow(); showing it
    // here could briefly place it at the overlay's pre-layout origin.
    if (!m_mosaicCoachmarkPending) {
        showMosaicHint(MosaicHintDisplay::HoverReminder, anchor);
    }
}

void QmlFloatingSubToolbar::onMosaicBrushPreviewHoverExited()
{
    m_mosaicPreviewHovered = false;
    m_suppressHoverReminderUntilExit = false;
    if (m_mosaicHintDisplay == MosaicHintDisplay::HoverReminder) {
        hideMosaicHint();
    }
}

void QmlFloatingSubToolbar::showPendingMosaicCoachmark()
{
    if (!m_mosaicCoachmarkPending || !m_mosaicHintActivationActive
        || m_mosaicBrushAdjustmentLearned || !m_viewModel->isMosaicActive()
        || !m_view || !m_view->isVisible()) {
        return;
    }

    const QRect anchor = mosaicPreviewGlobalRect();
    if (anchor.isEmpty()) {
        return;
    }

    m_mosaicCoachmarkPending = false;
    showMosaicHint(MosaicHintDisplay::Coachmark, anchor);
    m_mosaicCoachmarkTimer.start();
}

void QmlFloatingSubToolbar::showMosaicHint(MosaicHintDisplay display,
                                           const QRect& anchorGlobalRect)
{
    if (!m_view || !m_view->isVisible() || anchorGlobalRect.isEmpty()) {
        return;
    }

    const QString text = m_viewModel->mosaicBrushHintText();
    if (text.isEmpty()) {
        hideMosaicHint();
        return;
    }

    // Keep the tooltip outside the whole strip while preserving the preview's
    // horizontal center as its visual anchor.
    QRect placementAnchor = anchorGlobalRect;
    placementAnchor.setTop(qMin(placementAnchor.top(), m_view->y()));
    placementAnchor.setBottom(qMax(placementAnchor.bottom(),
                                   m_view->y() + m_view->height() - 1));

    m_mosaicHintDisplay = display;
    setMosaicHintEmphasis(true);
    m_tooltip.showFor(text, placementAnchor, m_view, m_mosaicHintPlacement);
}

void QmlFloatingSubToolbar::hideMosaicHint()
{
    m_tooltip.hide();
    m_mosaicHintDisplay = MosaicHintDisplay::None;
    setMosaicHintEmphasis(false);
}

void QmlFloatingSubToolbar::setMosaicHintEmphasis(bool active)
{
    if (m_rootItem) {
        m_rootItem->setProperty("mosaicBrushHintActive", active);
    }
}

QRect QmlFloatingSubToolbar::mosaicPreviewGlobalRect() const
{
    if (!m_rootItem) {
        return {};
    }

    auto* preview = m_rootItem->findChild<QQuickItem*>(
        QStringLiteral("widthPreviewContainer"));
    if (!preview || !preview->isVisible()) {
        return {};
    }

    const QPointF topLeft = preview->mapToGlobal(QPointF(0.0, 0.0));
    return QRect(qRound(topLeft.x()),
                 qRound(topLeft.y()),
                 qMax(1, qRound(preview->width())),
                 qMax(1, qRound(preview->height())));
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
    m_mosaicHintPlacement = targetPos.y() > toolbarRect.center().y()
        ? TooltipPlacement::Below
        : TooltipPlacement::Above;
    syncCursorSurface();

    if (m_mosaicCoachmarkPending) {
        showPendingMosaicCoachmark();
    } else if (m_mosaicHintDisplay != MosaicHintDisplay::None) {
        const QRect anchor = mosaicPreviewGlobalRect();
        if (!anchor.isEmpty()) {
            showMosaicHint(m_mosaicHintDisplay, anchor);
        }
    }
}

} // namespace SnapTray
