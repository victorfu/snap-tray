#include "qml/QmlWindowedSubToolbar.h"
#include "qml/QmlOverlayManager.h"
#include "qml/PinToolOptionsViewModel.h"
#include "platform/WindowLevel.h"

#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QMouseEvent>
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

QmlWindowedSubToolbar::QmlWindowedSubToolbar(QObject* parent)
    : QObject(parent)
    , m_viewModel(new PinToolOptionsViewModel(this))
{
    // Forward emojiPickerRequested from ViewModel
    connect(m_viewModel, &PinToolOptionsViewModel::emojiPickerRequested,
            this, &QmlWindowedSubToolbar::emojiPickerRequested);
}

QmlWindowedSubToolbar::~QmlWindowedSubToolbar()
{
    if (m_view)
        m_view->removeEventFilter(this);
    destroyQuickView(m_view, m_rootItem);
}

void QmlWindowedSubToolbar::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay();
    // This tool-options strip should not take focus away from the PinWindow.
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_view->setCursor(Qt::ArrowCursor);
    m_view->rootContext()->setContextProperty(QStringLiteral("pinToolOptionsViewModel"), m_viewModel);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolOptionsStrip.qml")));

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "ToolOptionsStrip QML error:" << error.toString();
    }

    m_rootItem = m_view->rootObject();
    if (m_rootItem) {
        m_rootItem->setProperty("viewModel",
                                QVariant::fromValue(static_cast<QObject*>(m_viewModel)));
    }

    m_view->installEventFilter(this);
}

void QmlWindowedSubToolbar::applyPlatformWindowFlags()
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

    [window setLevel:NSFloatingWindowLevel];
    [window setHidesOnDeactivate:NO];

    if ([window isKindOfClass:[NSPanel class]]) {
        [(NSPanel*)window setBecomesKeyOnlyIfNeeded:YES];
    }

    NSUInteger mask = [window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [window setStyleMask:mask];
#endif
}

// ── Show / Hide / Close ──

void QmlWindowedSubToolbar::show()
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

void QmlWindowedSubToolbar::hide()
{
    if (m_view)
        m_view->hide();
    emit cursorSyncRequested();
}

void QmlWindowedSubToolbar::close()
{
    if (m_view)
        m_view->removeEventFilter(this);
    destroyQuickView(m_view, m_rootItem);
}

bool QmlWindowedSubToolbar::isVisible() const
{
    return m_view && m_view->isVisible();
}

QRect QmlWindowedSubToolbar::geometry() const
{
    if (!m_view)
        return QRect();
    return QRect(m_view->position(), m_view->size());
}

PinToolOptionsViewModel* QmlWindowedSubToolbar::viewModel() const
{
    return m_viewModel;
}

bool QmlWindowedSubToolbar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_view) {
        if (event->type() == QEvent::MouseButtonPress &&
            m_rootItem &&
            m_rootItem->property("hasOpenStyleMenu").toBool()) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QVariant localX(mouseEvent->position().x());
            const QVariant localY(mouseEvent->position().y());
            QVariant insideStyleControls(false);

            const bool invoked = QMetaObject::invokeMethod(
                m_rootItem,
                "styleControlContainsLocalPoint",
                Q_RETURN_ARG(QVariant, insideStyleControls),
                Q_ARG(QVariant, localX),
                Q_ARG(QVariant, localY));

            if (!invoked || !insideStyleControls.toBool()) {
                QMetaObject::invokeMethod(m_rootItem, "closeStyleMenus");
            }
        }

        if (shouldReassertNativeArrow(event->type())) {
            reassertNativeArrowForView(m_view);
            emit cursorSyncRequested();
        }
        if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            emit cursorRestoreRequested();
        }
    }

    return QObject::eventFilter(obj, event);
}

// ── Tool display ──

void QmlWindowedSubToolbar::showForTool(int toolId)
{
    m_viewModel->showForTool(toolId);

    if (m_viewModel->hasContent()) {
        show();
    } else {
        hide();
    }
}

// ── Positioning ──

void QmlWindowedSubToolbar::positionBelow(const QRect& toolbarRect)
{
    if (!m_view)
        return;

    QScreen* screen = QGuiApplication::screenAt(toolbarRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    const QRect screenGeom = screen->geometry();
    const int w = m_view->width();
    const int h = m_view->height();
    constexpr int kMargin = 4;

    // Left-aligned with toolbar, below it
    int x = toolbarRect.left();
    int y = toolbarRect.bottom() + kMargin;

    // If would go off bottom, position above
    if (y + h > screenGeom.bottom() - 10)
        y = toolbarRect.top() - h - kMargin;

    // Keep on screen horizontally
    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - w - 10);

    m_view->setPosition(x, y);
}

} // namespace SnapTray
