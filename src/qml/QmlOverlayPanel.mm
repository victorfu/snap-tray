#include "qml/QmlOverlayPanel.h"
#include "qml/QmlOverlayManager.h"
#include "platform/WindowLevel.h"

#include <QEvent>
#include <QQuickView>
#include <QQuickItem>
#include <QVariantList>
#include <QWidget>
#include <QVariant>

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

QmlOverlayPanel::QmlOverlayPanel(const QUrl& qmlSource,
                                 QObject* viewModel,
                                 const QString& contextPropertyName,
                                 const Options& options,
                                 QObject* parent)
    : QObject(parent)
    , m_qmlSource(qmlSource)
    , m_viewModel(viewModel)
    , m_contextPropertyName(contextPropertyName)
    , m_options(options)
{
}

QmlOverlayPanel::~QmlOverlayPanel()
{
    if (m_view) {
        m_view->removeEventFilter(this);
    }
    destroyQuickView(m_view, m_rootItem);
}

void QmlOverlayPanel::onWindowMaskRectsChanged()
{
    updateWindowMask();
}

void QmlOverlayPanel::ensureView()
{
    if (m_view)
        return;

    m_view = QmlOverlayManager::instance().createScreenOverlay();

    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, !m_options.acceptsFocus);
    if (m_options.transparentForInput) {
        m_view->setFlag(Qt::WindowTransparentForInput, true);
    }

    m_view->setResizeMode(QQuickView::SizeViewToRootObject);
    m_view->setCursor(Qt::ArrowCursor);

    const QVariantMap initialProperties{
        {m_contextPropertyName, QVariant::fromValue(m_viewModel)},
    };
    m_view->setInitialProperties(initialProperties);
    m_view->setSource(m_qmlSource);

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "QmlOverlayPanel QML error:" << error.toString();
    }

    m_rootItem = m_view->rootObject();
    if (m_rootItem && m_rootItem->metaObject()->indexOfSignal("windowMaskRectsChanged()") >= 0) {
        connect(m_rootItem, SIGNAL(windowMaskRectsChanged()),
                this, SLOT(onWindowMaskRectsChanged()));
    }

    m_view->installEventFilter(this);
    updateWindowMask();
}

void QmlOverlayPanel::applyPlatformWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView* nsView = reinterpret_cast<NSView*>(m_view->winId());
    if (!nsView)
        return;

    NSWindow* window = [nsView window];
    if (!window)
        return;

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

    [window setOpaque:NO];
#endif
}

void QmlOverlayPanel::show()
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

void QmlOverlayPanel::hide()
{
    if (m_view) {
        m_view->hide();
    }
    emit cursorSyncRequested();
}

void QmlOverlayPanel::close()
{
    if (m_view) {
        m_view->removeEventFilter(this);
    }
    destroyQuickView(m_view, m_rootItem);
}

bool QmlOverlayPanel::isVisible() const
{
    return m_view && m_view->isVisible();
}

QRect QmlOverlayPanel::geometry() const
{
    if (!m_view)
        return QRect();
    return QRect(m_view->position(), m_view->size());
}

bool QmlOverlayPanel::containsGlobalPoint(const QPoint& globalPos) const
{
    if (!m_view || !m_view->isVisible()) {
        return false;
    }

    const QPoint localPos = globalPos - m_view->position();
    if (!m_windowMask.isEmpty()) {
        return m_windowMask.contains(localPos);
    }

    return QRect(QPoint(0, 0), m_view->size()).contains(localPos);
}

void QmlOverlayPanel::setPosition(const QPoint& globalPos)
{
    if (m_view)
        m_view->setPosition(globalPos);
}

void QmlOverlayPanel::resize(const QSize& size)
{
    if (!m_view)
        return;

    if (m_rootItem) {
        m_rootItem->setSize(QSizeF(size));
    } else {
        m_view->resize(size);
    }
}

void QmlOverlayPanel::setParentWidget(QWidget* parent)
{
    m_parentWidget = parent;
}

void QmlOverlayPanel::updateWindowMask()
{
    if (!m_view || !m_rootItem) {
        m_windowMask = QRegion();
        return;
    }

    QRegion newMask;
    const QVariant rectsVariant = m_rootItem->property("windowMaskRects");
    const QVariantList rects = rectsVariant.toList();
    for (const QVariant& rectVariant : rects) {
        QRect rect = rectVariant.toRect();
        if (!rect.isValid()) {
            rect = rectVariant.toRectF().toAlignedRect();
        }
        if (rect.isValid() && !rect.isEmpty()) {
            newMask += QRegion(rect);
        }
    }

    m_windowMask = newMask;
    m_view->setMask(m_windowMask);
}

bool QmlOverlayPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view) {
        if (shouldReassertNativeArrow(event->type())) {
            reassertNativeArrowForView(m_view);
            emit cursorSyncRequested();
        }
        if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            emit cursorRestoreRequested();
        }
    }

    return QObject::eventFilter(watched, event);
}

QQuickView* QmlOverlayPanel::view() const
{
    return m_view;
}

} // namespace SnapTray
