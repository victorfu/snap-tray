#include "qml/QmlFloatingSubToolbar.h"
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
    if (!view || !view->isVisible())
        return;
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
    , m_ownsViewModel(true)
{
    connect(m_viewModel, &PinToolOptionsViewModel::emojiPickerRequested,
            this, &QmlFloatingSubToolbar::emojiPickerRequested);
}

QmlFloatingSubToolbar::~QmlFloatingSubToolbar()
{
    if (m_view)
        m_view->removeEventFilter(this);
    destroyQuickView(m_view, m_rootItem);
}

void QmlFloatingSubToolbar::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay();
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_view->setCursor(Qt::ArrowCursor);
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

    if ([window isKindOfClass:[NSPanel class]]) {
        [(NSPanel*)window setBecomesKeyOnlyIfNeeded:YES];
    }

    NSUInteger mask = [window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [window setStyleMask:mask];
#endif
}

// ── Show / Hide / Close ──

void QmlFloatingSubToolbar::show()
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

void QmlFloatingSubToolbar::hide()
{
    if (m_view)
        m_view->hide();
    emit cursorSyncRequested();
}

void QmlFloatingSubToolbar::close()
{
    if (m_view)
        m_view->removeEventFilter(this);
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

PinToolOptionsViewModel* QmlFloatingSubToolbar::viewModel() const
{
    return m_viewModel;
}

void QmlFloatingSubToolbar::setParentWidget(QWidget* parent)
{
    m_parentWidget = parent;
}

bool QmlFloatingSubToolbar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_view) {
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

    m_view->setPosition(x, y);
}

} // namespace SnapTray
