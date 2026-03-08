#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlOverlayManager.h"
#include "platform/WindowLevel.h"

#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QScreen>
#include <QGuiApplication>
#include <QTimer>
#include <QMouseEvent>
#include <QCursor>
#include <QWidget>
#include <QVariant>
#include <QtMath>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace SnapTray {

namespace {

bool shouldReassertNativeArrow(QEvent::Type type)
{
    switch (type) {
    case QEvent::Enter:
    case QEvent::HoverMove:
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        return true;
    default:
        return false;
    }
}

void reassertNativeArrowForView(QQuickView* view)
{
    if (!view || !view->isVisible()) {
        return;
    }
    forceNativeArrowCursor();
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

QmlFloatingToolbar::QmlFloatingToolbar(QObject* viewModel, QObject* parent)
    : QObject(parent)
    , m_viewModel(viewModel)
{
}

QmlFloatingToolbar::QmlFloatingToolbar(QObject* viewModel, const Appearance& appearance, QObject* parent)
    : QObject(parent)
    , m_viewModel(viewModel)
    , m_appearance(appearance)
{
}

QmlFloatingToolbar::~QmlFloatingToolbar()
{
    destroyQuickView(m_tooltipView, m_tooltipRootItem);

    if (m_view)
        m_view->removeEventFilter(this);
    destroyQuickView(m_view, m_rootItem);
}

void QmlFloatingToolbar::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay();
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_view->setCursor(Qt::ArrowCursor);

    const QVariantMap initialProperties{
        {QStringLiteral("viewModel"),
         QVariant::fromValue(static_cast<QObject*>(m_viewModel))},
    };
    m_view->setInitialProperties(initialProperties);
    m_view->setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/FloatingToolbar.qml")));

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "QmlFloatingToolbar QML error:" << error.toString();
    }

    m_rootItem = m_view->rootObject();
    if (m_rootItem) {
        applyAppearance();
    }

    m_view->installEventFilter(this);

    if (m_rootItem)
        setupConnections();
}

void QmlFloatingToolbar::applyAppearance()
{
    if (!m_rootItem) {
        return;
    }

    m_rootItem->setProperty("iconPalette",
                            static_cast<int>(m_appearance.iconPalette));
    if (m_appearance.iconNormalColor.isValid()) {
        m_rootItem->setProperty("iconNormalColor", m_appearance.iconNormalColor);
    }
    if (m_appearance.iconActionColor.isValid()) {
        m_rootItem->setProperty("iconActionColor", m_appearance.iconActionColor);
    }
    if (m_appearance.iconCancelColor.isValid()) {
        m_rootItem->setProperty("iconCancelColor", m_appearance.iconCancelColor);
    }
    if (m_appearance.iconActiveColor.isValid()) {
        m_rootItem->setProperty("iconActiveColor", m_appearance.iconActiveColor);
    }
}

void QmlFloatingToolbar::ensureTooltipView()
{
    if (m_tooltipView)
        return;

    m_tooltipView = QmlOverlayManager::instance().createParentOverlay(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/components/RecordingTooltip.qml")));
    m_tooltipView->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_tooltipView->setResizeMode(QQuickView::SizeRootObjectToView);
    m_tooltipView->setFlag(Qt::WindowTransparentForInput, true);
    m_tooltipView->setCursor(Qt::ArrowCursor);

    if (m_tooltipView->status() == QQuickView::Error) {
        for (const auto& error : m_tooltipView->errors())
            qWarning() << "QmlFloatingToolbar tooltip QML error:" << error.toString();
    }

    m_tooltipRootItem = m_tooltipView->rootObject();
}

void QmlFloatingToolbar::setupConnections()
{
    if (!m_rootItem)
        return;

    auto connectOrWarn = [this](const char* signal, const char* slot, const char* description) {
        if (!connect(m_rootItem, signal, this, slot))
            qWarning() << "QmlFloatingToolbar: failed to connect" << description;
    };

    connectOrWarn(SIGNAL(buttonHovered(int,double,double,double,double)),
                  SLOT(onButtonHovered(int,double,double,double,double)),
                  "buttonHovered");
    connectOrWarn(SIGNAL(buttonUnhovered()),
                  SLOT(onButtonUnhovered()),
                  "buttonUnhovered");

    connectOrWarn(SIGNAL(dragStarted()), SLOT(onDragStarted()), "dragStarted");
    connectOrWarn(SIGNAL(dragFinished()), SLOT(onDragFinished()), "dragFinished");
    connectOrWarn(SIGNAL(dragMoved(double,double)),
                  SLOT(onDragMoved(double,double)),
                  "dragMoved");
}

void QmlFloatingToolbar::applyPlatformWindowFlags()
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

    // Set toolbar one level above parent so it stays visible over fullscreen widgets
    // (e.g., RegionSelector at kCGScreenSaverWindowLevel covers entire screen)
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

    if ([window isKindOfClass:[NSPanel class]]) {
        [(NSPanel*)window setBecomesKeyOnlyIfNeeded:YES];
    }

    NSUInteger mask = [window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [window setStyleMask:mask];
#endif
}

void QmlFloatingToolbar::applyTooltipWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_tooltipView)
        return;

    NSView* view = reinterpret_cast<NSView*>(m_tooltipView->winId());
    if (!view)
        return;

    NSWindow* window = [view window];
    if (!window)
        return;

    NSInteger targetLevel = NSPopUpMenuWindowLevel;
    if (m_view) {
        NSView* toolbarNsView = reinterpret_cast<NSView*>(m_view->winId());
        if (toolbarNsView) {
            NSWindow* toolbarWindow = [toolbarNsView window];
            if (toolbarWindow) {
                targetLevel = qMax<NSInteger>(targetLevel, [toolbarWindow level] + 1);
            }
        }
    }

    [window setLevel:targetLevel];
    [window setHidesOnDeactivate:NO];
    [window setIgnoresMouseEvents:YES];
    [window setHasShadow:YES];
    [window setSharingType:NSWindowSharingNone];
#endif
}

// ── Show / Hide / Close ──

void QmlFloatingToolbar::show()
{
    ensureView();

    if (!m_rootItem)
        return;

    m_view->show();
    applyPlatformWindowFlags();
    QmlOverlayManager::enableNativeShadow(m_view);
    m_view->raise();
    emit cursorSyncRequested();
}

void QmlFloatingToolbar::hide()
{
    hideTooltip();
    if (m_view)
        m_view->hide();
    emit cursorSyncRequested();
}

void QmlFloatingToolbar::close()
{
    hideTooltip();
    if (m_view)
        m_view->removeEventFilter(this);
    destroyQuickView(m_view, m_rootItem);
    destroyQuickView(m_tooltipView, m_tooltipRootItem);
}

bool QmlFloatingToolbar::isVisible() const
{
    return m_view && m_view->isVisible();
}

QRect QmlFloatingToolbar::geometry() const
{
    if (!m_view)
        return QRect();
    return QRect(m_view->position(), m_view->size());
}

int QmlFloatingToolbar::width() const
{
    return m_view ? m_view->width() : 0;
}

int QmlFloatingToolbar::height() const
{
    return m_view ? m_view->height() : 0;
}

void QmlFloatingToolbar::setParentWidget(QWidget* parent)
{
    m_parentWidget = parent;
}

// ── Positioning ──

void QmlFloatingToolbar::positionForSelection(const QRect& selectionRect,
                                              int viewportWidth, int viewportHeight,
                                              HorizontalAlignment alignment)
{
    ensureView();
    if (!m_view)
        return;

    const int w = m_view->width();
    const int h = m_view->height();
    constexpr int kMargin = 8;

    int x = 0;
    switch (alignment) {
    case HorizontalAlignment::RightEdge:
        // QRect::right() is inclusive; add 1 to align the toolbar's right edge.
        x = selectionRect.right() + 1 - w;
        break;
    case HorizontalAlignment::Center:
    default:
        x = selectionRect.center().x() - w / 2;
        break;
    }
    int y = selectionRect.bottom() + kMargin;

    // If toolbar would go off bottom, position above
    if (y + h > viewportHeight - 10)
        y = selectionRect.top() - h - kMargin;

    // If still off screen, position inside at bottom
    if (y < 10)
        y = selectionRect.bottom() - h - 10;

    // Keep on screen horizontally
    x = qBound(10, x, viewportWidth - w - 10);

    // Convert from widget-local to screen coordinates if we have a parent widget
    QPoint screenPos(x, y);
    if (m_parentWidget) {
        screenPos = m_parentWidget->mapToGlobal(QPoint(x, y));
    }

    m_view->setPosition(screenPos);
}

void QmlFloatingToolbar::positionAt(int centerX, int bottomY)
{
    ensureView();
    if (!m_view)
        return;

    const int w = m_view->width();
    const int h = m_view->height();

    QPoint pos(centerX - w / 2, bottomY - h);

    if (m_parentWidget) {
        pos = m_parentWidget->mapToGlobal(pos);
    }

    m_view->setPosition(pos);
}

void QmlFloatingToolbar::setPosition(const QPoint& pos)
{
    if (m_view)
        m_view->setPosition(pos);
}

// ── Event filter (cursor management) ──

bool QmlFloatingToolbar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_view) {
        if (shouldReassertNativeArrow(event->type())) {
            reassertNativeArrowForView(m_view);
            emit cursorSyncRequested();
        }
        if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            hideTooltip();
            emit cursorRestoreRequested();
        }
    }
    return QObject::eventFilter(obj, event);
}

// ── Tooltip management ──

void QmlFloatingToolbar::onButtonHovered(int buttonId, double anchorX, double anchorY,
                                         double anchorW, double anchorH)
{
    if (!m_view)
        return;

    reassertNativeArrowForView(m_view);
    emit cursorSyncRequested();

    // Find tooltip text from ViewModel's button list
    QString tip;
    QVariantList buttons = m_viewModel->property("buttons").toList();
    for (const auto& btn : buttons) {
        QVariantMap map = btn.toMap();
        if (map["id"].toInt() == buttonId) {
            tip = map["tooltip"].toString();
            break;
        }
    }

    if (tip.isEmpty()) {
        hideTooltip();
        return;
    }

    QPoint globalPos(m_view->x() + qRound(anchorX),
                     m_view->y() + qRound(anchorY));
    QRect anchorRect(globalPos, QSize(qRound(anchorW), qRound(anchorH)));

    showTooltip(tip, anchorRect);
}

void QmlFloatingToolbar::onButtonUnhovered()
{
    hideTooltip();
}

void QmlFloatingToolbar::showTooltip(const QString& text, const QRect& anchorRect)
{
    ensureTooltipView();
    if (!m_tooltipView || !m_tooltipRootItem || !m_view)
        return;

    const quint64 requestId = ++m_tooltipRequestId;
    m_tooltipRootItem->setProperty("tooltipText", text);
    m_tooltipRootItem->polish();

    QTimer::singleShot(0, this, [this, requestId, anchorRect]() {
        if (requestId != m_tooltipRequestId || !m_tooltipView || !m_tooltipRootItem || !m_view)
            return;

        const int tipWidth = qMax(1, qCeil(m_tooltipRootItem->implicitWidth()));
        const int tipHeight = qMax(1, qCeil(m_tooltipRootItem->implicitHeight()));
        const QPoint anchorCenter = anchorRect.center();
        const int barTop = m_view->y();

        auto positionTooltip = [this, tipWidth, tipHeight](const QPoint& anchorEdge, bool above) {
            int x = anchorEdge.x() - tipWidth / 2;
            int y = above ? anchorEdge.y() - tipHeight - 6 : anchorEdge.y() + 6;

            if (QScreen* screen = QGuiApplication::screenAt(anchorEdge)) {
                const QRect bounds = screen->availableGeometry();
                x = qBound(bounds.left() + 5, x, bounds.right() - tipWidth - 5);
            }

            m_tooltipView->setGeometry(x, y, tipWidth, tipHeight);
        };

        positionTooltip(QPoint(anchorCenter.x(), barTop), true);
        m_tooltipView->show();
        applyTooltipWindowFlags();
        m_tooltipView->raise();

        // Fallback: if above goes off screen, show below
        QScreen* screen = QGuiApplication::screenAt(anchorCenter);
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect bounds = screen->availableGeometry();
            const QRect tipGeom = m_tooltipView->geometry();
            if (tipGeom.top() < bounds.top() + 5) {
                const int barBottom = m_view->y() + m_view->height();
                positionTooltip(QPoint(anchorCenter.x(), barBottom), false);
            }
        }
    });
}

void QmlFloatingToolbar::hideTooltip()
{
    ++m_tooltipRequestId;
    if (m_tooltipView)
        m_tooltipView->hide();
}

// ── Drag handling ──

void QmlFloatingToolbar::onDragStarted()
{
    m_isDragging = true;
    if (m_view)
        m_dragStartViewPos = m_view->position();
    m_dragStartCursorPos = QCursor::pos();
    hideTooltip();
    emit dragStarted();
}

void QmlFloatingToolbar::onDragFinished()
{
    m_isDragging = false;
    emit dragFinished();
}

void QmlFloatingToolbar::onDragMoved(double deltaX, double deltaY)
{
    Q_UNUSED(deltaX)
    Q_UNUSED(deltaY)

    if (!m_view || !m_isDragging)
        return;

    QPoint newPos = m_dragStartViewPos + (QCursor::pos() - m_dragStartCursorPos);

    QScreen* screen = QGuiApplication::screenAt(
        newPos + QPoint(m_view->width() / 2, m_view->height() / 2));
    if (screen) {
        const QRect screenGeom = screen->geometry();
        newPos.setX(qBound(screenGeom.left(), newPos.x(), screenGeom.right() - m_view->width()));
        newPos.setY(qBound(screenGeom.top(), newPos.y(), screenGeom.bottom() - m_view->height()));
    }

    m_view->setPosition(newPos);
    hideTooltip();
    const QPoint cursorDelta = QCursor::pos() - m_dragStartCursorPos;
    emit dragMoved(cursorDelta.x(), cursorDelta.y());
}

} // namespace SnapTray
