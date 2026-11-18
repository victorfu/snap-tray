#include "qml/RecordingPrompter.h"
#include "qml/QmlOverlayManager.h"
#include "../platform/WindowLevel.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

namespace SnapTray {
bool RecordingPrompter::separate(QScreen* capture, QScreen* display)
{
    return capture && display && capture != display && QGuiApplication::screens().contains(display)
        && QGuiApplication::screens().contains(capture) && !capture->geometry().isEmpty()
        && !display->geometry().isEmpty() && !capture->geometry().intersects(display->geometry());
}

RecordingPrompter::RecordingPrompter(QScreen* capture, QScreen* display, QString script, int fontSize)
    : m_capture(capture), m_display(display), m_script(std::move(script))
{
    if (!separate(capture, display)) return;
    m_view.reset(QmlOverlayManager::instance().createUtilityWindow());
    Qt::WindowFlags flags = Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus;
    if (QGuiApplication::platformName() == QStringLiteral("xcb"))
        flags |= Qt::X11BypassWindowManagerHint; // prevent WM relocation onto the recorded output
    m_view->setFlags(flags);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setScreen(display);
    const QRect available = display->availableGeometry();
    const QSize size = available.size().boundedTo(QSize(740, 420));
    QRect panel(QPoint(), size);
    panel.moveCenter(available.center());
    m_view->setGeometry(panel);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingPrompterView.qml")));
    if (!m_view->rootObject()) return;
    m_view->rootObject()->setProperty("fontPixels", fontSize);
    // Stop showing the script on a layout change, including a monitor being
    // unplugged and the window manager rehoming its windows.
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { disable(); });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] { disable(); });
    for (auto* screen : {capture, display})
        connect(screen, &QScreen::geometryChanged, this, [this] { disable(); });
    connect(m_view.get(), &QWindow::screenChanged, this, [this] { checkPlacement(); });
    connect(m_view.get(), &QWindow::xChanged, this, [this] { checkPlacement(); });
    connect(m_view.get(), &QWindow::yChanged, this, [this] { checkPlacement(); });
    connect(m_view.get(), &QWindow::widthChanged, this, [this] { checkPlacement(); });
    connect(m_view.get(), &QWindow::heightChanged, this, [this] { checkPlacement(); });
    connect(m_view.get(), &QQuickWindow::frameSwapped, this, [this] { checkPlacement(); }, Qt::QueuedConnection);
    m_view->create();
    setWindowExcludedFromCapture(m_view.get(), true);
    m_view->show();
    QmlOverlayManager::preventWindowHideOnDeactivate(m_view.get());
}

void RecordingPrompter::checkPlacement()
{
    if (m_disabled || !m_view || !m_view->isExposed()) return;
    if (!separate(m_capture, m_display) || m_view->screen() != m_display
        || !m_display->geometry().contains(m_view->frameGeometry())) {
        disable();
        return;
    }
    if (m_reading && !m_script.isEmpty()) {
        m_view->rootObject()->setProperty("script", m_script);
        m_script.clear();
    }
}

void RecordingPrompter::disable()
{
    m_disabled = true;
    m_script.clear();
    if (m_view) {
        m_view->hide();
        if (m_view->rootObject()) m_view->rootObject()->setProperty("script", QString());
    }
}

quintptr RecordingPrompter::captureExclusionId() const
{
    if (m_disabled || !m_view || !m_view->rootObject()) return 0;
#ifdef Q_OS_MACOS
    NSView* view = reinterpret_cast<NSView*>(m_view->winId());
    const NSInteger number = view.window.windowNumber;
    return number > 0 ? static_cast<quintptr>(number) : 0;
#elif defined(Q_OS_WIN)
    const auto hwnd = reinterpret_cast<HWND>(m_view->winId());
    DWORD affinity = WDA_NONE;
    if (!GetWindowDisplayAffinity(hwnd, &affinity) || affinity == WDA_NONE) return 0;
    return reinterpret_cast<quintptr>(hwnd);
#else
    return static_cast<quintptr>(m_view->winId());
#endif
}

void RecordingPrompter::setReading(bool reading)
{
    m_reading = reading;
    if (m_view && m_view->rootObject()) {
        m_view->rootObject()->setEnabled(reading);
        checkPlacement();
    }
}

RecordingPrompter::~RecordingPrompter() { disable(); }
}
