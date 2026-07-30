#include "qml/ToolbarTooltipController.h"
#include "qml/QmlOverlayManager.h"

#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <QtMath>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace SnapTray {

namespace {

constexpr int kTooltipGap = 6;
constexpr int kScreenEdgeMargin = 5;

#ifdef Q_OS_MACOS
NSWindow* nsWindowForWidget(const QWidget* widget)
{
    if (!widget) {
        return nil;
    }

    NSView* view = reinterpret_cast<NSView*>(widget->winId());
    return view ? [view window] : nil;
}

NSWindow* nsWindowForQuickView(const QQuickView* view)
{
    if (!view) {
        return nil;
    }

    NSView* nsView = reinterpret_cast<NSView*>(view->winId());
    return nsView ? [nsView window] : nil;
}
#endif

} // namespace

ToolbarTooltipController::ToolbarTooltipController(QObject* parent)
    : QObject(parent)
{
}

ToolbarTooltipController::~ToolbarTooltipController()
{
    if (!m_view) {
        return;
    }

    m_view->close();
    delete m_view;
    m_view = nullptr;
    m_rootItem = nullptr;
}

void ToolbarTooltipController::ensureView()
{
    if (m_view) {
        return;
    }

    m_view = QmlOverlayManager::instance().createParentOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/components/RecordingTooltip.qml")));
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setFlag(Qt::WindowTransparentForInput, true);

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors()) {
            qWarning() << "Toolbar tooltip QML error:" << error.toString();
        }
    }

    m_rootItem = m_view->rootObject();
}

void ToolbarTooltipController::showFor(const QString& text,
                                       const QRect& anchorGlobalRect,
                                       QQuickView* owner,
                                       TooltipPlacement preferred)
{
    if (text.isEmpty() || !owner) {
        hide();
        return;
    }

    ensureView();
    if (!m_view || !m_rootItem) {
        return;
    }

    m_view->setTransientParent(owner);
    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
    if (m_view->isVisible()) {
        applyWindowFlags(owner);
    }

    const quint64 requestId = ++m_requestId;
    m_rootItem->setProperty("tooltipText", text);
    m_rootItem->polish();

    const QPointer<QQuickView> ownerGuard(owner);
    QTimer::singleShot(0, this, [this, requestId, anchorGlobalRect, ownerGuard, preferred]() {
        if (requestId != m_requestId || !m_view || !m_rootItem || !ownerGuard) {
            return;
        }

        const int tipWidth = qMax(1, qCeil(m_rootItem->implicitWidth()));
        const int tipHeight = qMax(1, qCeil(m_rootItem->implicitHeight()));
        const QPoint anchorCenter = anchorGlobalRect.center();
        const int ownerTop = ownerGuard->y();
        const int ownerBottom = ownerGuard->y() + ownerGuard->height();

        auto positionTooltip = [this, tipWidth, tipHeight](const QPoint& anchorEdge,
                                                           TooltipPlacement placement) {
            int x = anchorEdge.x() - tipWidth / 2;
            const int y = placement == TooltipPlacement::Above
                ? anchorEdge.y() - tipHeight - kTooltipGap
                : anchorEdge.y() + kTooltipGap;

            if (QScreen* screen = QGuiApplication::screenAt(anchorEdge)) {
                const QRect bounds = screen->availableGeometry();
                x = qBound(bounds.left() + kScreenEdgeMargin,
                           x,
                           bounds.right() - tipWidth - kScreenEdgeMargin);
            }

            m_view->setGeometry(x, y, tipWidth, tipHeight);
        };

        auto edgeFor = [anchorCenter, ownerTop, ownerBottom](TooltipPlacement placement) {
            return QPoint(anchorCenter.x(),
                          placement == TooltipPlacement::Above ? ownerTop : ownerBottom);
        };

        positionTooltip(edgeFor(preferred), preferred);
        m_view->show();
        applyWindowFlags(ownerGuard.data());
        m_view->raise();

        QScreen* screen = QGuiApplication::screenAt(anchorCenter);
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (!screen) {
            return;
        }

        const QRect bounds = screen->availableGeometry();
        const QRect tooltipGeometry = m_view->geometry();
        const bool preferredSideIsOffScreen = preferred == TooltipPlacement::Above
            ? tooltipGeometry.top() < bounds.top() + kScreenEdgeMargin
            : tooltipGeometry.bottom() > bounds.bottom() - kScreenEdgeMargin;
        if (preferredSideIsOffScreen) {
            const TooltipPlacement fallback = preferred == TooltipPlacement::Above
                ? TooltipPlacement::Below
                : TooltipPlacement::Above;
            positionTooltip(edgeFor(fallback), fallback);
        }
    });
}

void ToolbarTooltipController::hide()
{
    ++m_requestId;
    if (m_view) {
        m_view->hide();
    }
}

void ToolbarTooltipController::setAssociatedWidget(QWidget* widget)
{
    m_associatedWidget = widget;
}

QWindow* ToolbarTooltipController::window() const
{
    return m_view;
}

void ToolbarTooltipController::applyWindowFlags(QQuickView* owner)
{
#ifdef Q_OS_MACOS
    if (NSWindow* window = nsWindowForQuickView(m_view)) {
        NSInteger targetLevel = NSPopUpMenuWindowLevel;
        if (NSWindow* ownerWindow = nsWindowForQuickView(owner)) {
            targetLevel = qMax<NSInteger>(targetLevel, [ownerWindow level] + 1);
        }
        if (NSWindow* associatedWindow = nsWindowForWidget(m_associatedWidget.data())) {
            targetLevel = qMax<NSInteger>(targetLevel, [associatedWindow level] + 1);
        }

        [window setLevel:targetLevel];
        [window setHidesOnDeactivate:NO];
        [window setIgnoresMouseEvents:YES];
        [window setHasShadow:YES];
        [window setSharingType:NSWindowSharingNone];
    }
#else
    Q_UNUSED(owner)
#endif

    QmlOverlayManager::applyShownOverlayWindowPolicy(m_view);
}

} // namespace SnapTray
