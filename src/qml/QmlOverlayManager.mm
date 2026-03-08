#include "qml/QmlOverlayManager.h"
#include "qml/ThemeManager.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace SnapTray {

QmlOverlayManager::QmlOverlayManager(QObject* parent)
    : QObject(parent)
{
}

QmlOverlayManager& QmlOverlayManager::instance()
{
    static QmlOverlayManager inst;
    return inst;
}

void QmlOverlayManager::ensureEngine()
{
    if (m_engine)
        return;

    m_engine = new QQmlEngine(this);

    // For static QML modules linked via qt_add_qml_module, add the resource
    // root as an import path so the engine can resolve the SnapTrayQml module.
    m_engine->addImportPath(QStringLiteral("qrc:/"));

    // Expose ThemeManager as a context property so QML singletons (SemanticTokens,
    // ComponentTokens) can always resolve it, regardless of module loading order.
    m_engine->rootContext()->setContextProperty(
        QStringLiteral("ThemeManager"), &ThemeManager::instance());
}

QQmlEngine* QmlOverlayManager::engine() const
{
    // const_cast to allow lazy initialization from const getter
    const_cast<QmlOverlayManager*>(this)->ensureEngine();
    return m_engine;
}

QQuickView* QmlOverlayManager::createScreenOverlay()
{
    ensureEngine();

    auto* view = new QQuickView(m_engine, nullptr);
    view->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeViewToRootObject);

    return view;
}

QQuickView* QmlOverlayManager::createScreenOverlay(const QUrl& qmlUrl)
{
    auto* view = createScreenOverlay();
    view->setSource(qmlUrl);

    return view;
}

QQuickView* QmlOverlayManager::createParentOverlay(const QUrl& qmlUrl, QWidget* /*parent*/)
{
    ensureEngine();

    auto* view = new QQuickView(m_engine, nullptr);
    view->setFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeViewToRootObject);
    view->setSource(qmlUrl);

    return view;
}

QQuickView* QmlOverlayManager::createSettingsWindow()
{
    ensureEngine();

    auto* view = new QQuickView(m_engine, nullptr);
    view->setFlags(Qt::Window);
    view->setMinimumSize(QSize(860, 560));
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    // Note: setSource() is NOT called here. The caller (QmlSettingsWindow::ensureView)
    // must set context properties first, then call setSource() to avoid
    // "settingsBackend is not defined" errors during QML component creation.

    return view;
}

void QmlOverlayManager::preventWindowHideOnDeactivate(QQuickView* view)
{
#ifdef Q_OS_MACOS
    // LSUIElement apps hide all windows when the app deactivates (e.g. when
    // opening System Settings via NSWorkspace).  Apply this AFTER show() so
    // that Qt's platform integration does not reset the flag.
    if (!view)
        return;
    NSView* nsView = reinterpret_cast<NSView*>(view->winId());
    if (!nsView) {
        qWarning("preventWindowHideOnDeactivate: failed to obtain NSView from winId");
        return;
    }
    NSWindow* nsWindow = [nsView window];
    if (!nsWindow) {
        qWarning("preventWindowHideOnDeactivate: NSView has no parent NSWindow");
        return;
    }
    [nsWindow setHidesOnDeactivate:NO];
#else
    Q_UNUSED(view);
#endif
}

void QmlOverlayManager::enableNativeShadow(QQuickView* view)
{
#ifdef Q_OS_MACOS
    if (!view)
        return;
    NSView* nsView = reinterpret_cast<NSView*>(view->winId());
    if (!nsView)
        return;
    NSWindow* nsWindow = [nsView window];
    if (!nsWindow)
        return;
    [nsWindow setHasShadow:YES];
#else
    Q_UNUSED(view);
#endif
}

void QmlOverlayManager::configureInteractiveOverlayWindow(QQuickView* view)
{
#ifdef Q_OS_MACOS
    if (!view)
        return;

    NSView* nsView = reinterpret_cast<NSView*>(view->winId());
    if (!nsView)
        return;

    NSWindow* window = [nsView window];
    if (!window)
        return;

    [window setLevel:NSFloatingWindowLevel];
    [window setHidesOnDeactivate:NO];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];

    if ([window isKindOfClass:[NSPanel class]]) {
        [(NSPanel*)window setBecomesKeyOnlyIfNeeded:YES];
    }

    NSUInteger mask = [window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [window setStyleMask:mask];
#else
    Q_UNUSED(view);
#endif
}

} // namespace SnapTray
