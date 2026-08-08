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

int boundedCoordinate(int requested, int minimum, int maximum)
{
    return maximum < minimum ? minimum : qBound(minimum, requested, maximum);
}

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
    m_view->setFlag(Qt::WindowTransparentForInput, true);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);

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
    if (text.isEmpty() || anchorGlobalRect.isEmpty() || !owner) {
        hide();
        return;
    }

    ensureView();
    if (!m_view || !m_rootItem) {
        return;
    }

    m_view->setTransientParent(owner);
    m_rootItem->setProperty("tooltipText", text);
    m_rootItem->polish();

    const quint64 requestId = ++m_requestId;
    const QPointer<QQuickView> ownerGuard(owner);
    QTimer::singleShot(0, this,
        [this, requestId, anchorGlobalRect, ownerGuard, preferred]() {
            if (requestId != m_requestId || !m_view || !m_rootItem || !ownerGuard) {
                return;
            }

            QScreen* screen = QGuiApplication::screenAt(anchorGlobalRect.center());
            if (!screen) {
                screen = ownerGuard->screen();
            }
            if (!screen) {
                screen = QGuiApplication::primaryScreen();
            }
            if (!screen) {
                return;
            }

            const QRect screenBounds = screen->availableGeometry().isValid()
                ? screen->availableGeometry()
                : screen->geometry();
            const int usableLeft = screenBounds.left() + kScreenEdgeMargin;
            const int usableTop = screenBounds.top() + kScreenEdgeMargin;
            const int usableRightExclusive = screenBounds.right() + 1 - kScreenEdgeMargin;
            const int usableBottomExclusive = screenBounds.bottom() + 1 - kScreenEdgeMargin;
            const int usableWidth = qMax(1, usableRightExclusive - usableLeft);
            const int usableHeight = qMax(1, usableBottomExclusive - usableTop);

            const int tipWidth = qMin(usableWidth,
                qMax(1, qCeil(m_rootItem->implicitWidth())));
            const int tipHeight = qMin(usableHeight,
                qMax(1, qCeil(m_rootItem->implicitHeight())));

            auto yFor = [&](TooltipPlacement placement) {
                return placement == TooltipPlacement::Above
                    ? anchorGlobalRect.top() - kTooltipGap - tipHeight
                    : anchorGlobalRect.bottom() + 1 + kTooltipGap;
            };
            auto fitsVertically = [&](TooltipPlacement placement) {
                const int y = yFor(placement);
                return y >= usableTop && y + tipHeight <= usableBottomExclusive;
            };

            TooltipPlacement placement = preferred;
            if (!fitsVertically(preferred)) {
                const TooltipPlacement fallback = preferred == TooltipPlacement::Above
                    ? TooltipPlacement::Below
                    : TooltipPlacement::Above;
                if (fitsVertically(fallback)) {
                    placement = fallback;
                }
            }

            const int requestedX = anchorGlobalRect.center().x() - tipWidth / 2;
            const int x = boundedCoordinate(requestedX,
                                            usableLeft,
                                            usableRightExclusive - tipWidth);
            const int y = boundedCoordinate(yFor(placement),
                                            usableTop,
                                            usableBottomExclusive - tipHeight);
            m_view->setGeometry(x, y, tipWidth, tipHeight);
            m_view->show();
            applyWindowFlags(ownerGuard.data());
            m_view->raise();
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
