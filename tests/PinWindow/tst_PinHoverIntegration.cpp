#include <QtTest>
#include <QEnterEvent>
#include <QCursor>
#include <QHelpEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QQuickItem>
#include <QQuickView>
#include <QToolTip>
#include <QtQml/qqmlextensionplugin.h>

#include "PinWindow.h"
#include "pinwindow/PinHoverBar.h"
#include "PlatformFeatures.h"
#include "qml/PinHoverBarViewModel.h"
#include "settings/PinWindowSettingsManager.h"

#ifdef Q_OS_LINUX
#include <QtGui/qguiapplication_platform.h>
#include <X11/Xlib.h>
#endif

#ifdef Q_OS_WIN
#include "platform/CaptureExclusionPolicy.h"
#include <Windows.h>
#endif

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// #225: the hover bar inside a real pin.
//
// Hover is driven by synthetic events and a fake cursor provider, so the real
// pointer is parked off the pins for the whole suite. A pin shown under a
// resting cursor gets its OS hover handshake asynchronously, after
// qWaitForWindowExposed() and after the synthetic events. Windows CI parks the
// cursor at screen center, on the test pins: there the handshake delivered
// Enter, Leave, Enter, and the real Leave dismissed the Live hint or stopped
// the bar's show timer before the test observed them.
class TestPinHoverIntegration : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void hoverShowsBarWithValues();
    void x11HoverWindowsStackAbovePin_data();
    void x11HoverWindowsStackAbovePin();
    void barZoomRequestZoomsPin();
    void suppressedStates_data();
    void suppressedStates();
    void moreOpensContextMenu();
    void closingPinClosesBar();
    void rotateKeepsBarInside();
    void clickedBarComesBack();
    void liveTooltipOnlyOnDot();
    void liveIndicatorTooltipIsExcluded_data();
    void liveIndicatorTooltipIsExcluded();
    void liveCaptureExcludesHoverWindows();
    void sliderDragKeepsPanelInPlace();
    void transientHideKeepsPanel();

private:
    static QPixmap pixmap()
    {
        QPixmap p(480, 320);
        p.fill(Qt::darkCyan);
        return p;
    }
    // Shows the pin with the real pointer elsewhere; see the class comment.
    void showWithoutRealHover(PinWindow& window) const
    {
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        if (m_cursorParked) QVERIFY(!window.frameGeometry().contains(QCursor::pos()));
    }
    // Shows the pin and hovers it with the controller's cursor on the pin.
    void hover(PinWindow& window) const
    {
        showWithoutRealHover(window);
        if (QTest::currentTestFailed()) return;
        QEnterEvent enter(QPointF(100, 100), QPointF(100, 100), window.mapToGlobal(QPointF(100, 100)));
        QCoreApplication::sendEvent(&window, &enter);
        QVERIFY(window.m_hoverBar);
        window.m_hoverBar->setCursorPositionProvider([&window] { return window.mapToGlobal(QPoint(100, 100)); });
    }

    static constexpr int kCursorParkingInset = 8;
    QPoint m_originalCursor;
    bool m_cursorParked = false;
};

void TestPinHoverIntegration::initTestCase()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    m_originalCursor = QCursor::pos();
    // Test pins open near the center of the primary screen; a corner is clear of them.
    const QPoint parked = screen->availableGeometry().bottomRight()
        - QPoint(kCursorParkingInset, kCursorParkingInset);
    QCursor::setPos(parked);
    m_cursorParked = QCursor::pos() == parked;
    if (!m_cursorParked) qWarning("Could not park the system cursor; real hover events may race the tests.");
}

void TestPinHoverIntegration::cleanupTestCase()
{
    if (m_cursorParked) QCursor::setPos(m_originalCursor);
}

void TestPinHoverIntegration::hoverShowsBarWithValues()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    QCOMPARE(window.m_hoverBarViewModel->zoomPercent(), 100);
    QCOMPARE(window.m_hoverBarViewModel->opacityPercent(), 100);
    QVERIFY(window.frameGeometry().contains(window.m_hoverBar->barGeometry()));
}

void TestPinHoverIntegration::x11HoverWindowsStackAbovePin_data()
{
    QTest::addColumn<bool>("prepareCapture");
    QTest::newRow("lazy") << false;
    QTest::newRow("prepared-for-live-capture") << true;
}

void TestPinHoverIntegration::x11HoverWindowsStackAbovePin()
{
#ifndef Q_OS_LINUX
    QSKIP("X11 stacking regression");
#else
    auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11) QSKIP("Requires the xcb platform");
    Display* display = x11->display();
    QFETCH(bool, prepareCapture);
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    showWithoutRealHover(window);
    if (prepareCapture) window.liveCaptureExcludedWindowIds();
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);

    // XQueryTree lists siblings bottom-to-top. Follow WM frames to root
    // children so this detects the original bug under Mutter, not only Xvfb.
    const auto rootChild = [display](::Window child) {
        for (;;) {
            ::Window root = 0, parent = 0, *children = nullptr;
            unsigned int count = 0;
            const bool ok = XQueryTree(display, child, &root, &parent, &children, &count);
            if (children) XFree(children);
            if (!ok || !parent) return ::Window(0);
            if (parent == root) return child;
            child = parent;
        }
    };
    const auto abovePin = [&](QWindow* overlay) {
        XSync(display, False);
        const ::Window pinChild = rootChild(window.winId());
        const ::Window overlayChild = rootChild(overlay->winId());
        ::Window root = 0, parent = 0, *children = nullptr;
        unsigned int count = 0;
        if (!XQueryTree(display, DefaultRootWindow(display), &root, &parent, &children, &count))
            return false;
        int pinIndex = -1, overlayIndex = -1;
        for (unsigned int i = 0; i < count; ++i) {
            if (children[i] == pinChild) pinIndex = int(i);
            if (children[i] == overlayChild) overlayIndex = int(i);
        }
        if (children) XFree(children);
        XWindowAttributes attributes{};
        return pinIndex >= 0 && overlayIndex > pinIndex
            && XGetWindowAttributes(display, overlay->winId(), &attributes)
            && attributes.map_state == IsViewable && attributes.override_redirect;
    };
    QWindow* bar = window.m_hoverBar->barWindow();
    QVERIFY(QTest::qWaitForWindowExposed(bar));
    QTRY_VERIFY(abovePin(bar));
    QVERIFY(bar->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
    for (int panel : {PinHoverBarViewModel::ZoomPanel, PinHoverBarViewModel::OpacityPanel}) {
        window.m_hoverBarViewModel->openPanel(panel);
        QTRY_VERIFY(window.m_hoverBar->isPanelVisible());
        QWindow* panelWindow = window.m_hoverBar->panelWindow();
        QVERIFY(QTest::qWaitForWindowExposed(panelWindow));
        QTRY_VERIFY(abovePin(panelWindow));
        QVERIFY(panelWindow->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        window.m_hoverBarViewModel->closePanel();
    }
    window.m_hoverBar->hideNow();
    window.raise();
    window.m_hoverBar->showNow();
    QTRY_VERIFY(abovePin(bar));
#endif
}

void TestPinHoverIntegration::barZoomRequestZoomsPin()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    window.m_hoverBarViewModel->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(window.m_hoverBar->isPanelVisible());
    window.m_hoverBarViewModel->requestZoomPercent(150);
    QCOMPARE(window.zoomLevel(), 1.5);
    // Zooming from the panel keeps the bar and the panel open.  [RF 2]
    QTest::qWait(PinHoverBar::kHideDelayMs + 300);
    QVERIFY(window.m_hoverBar->isBarVisible());
    QVERIFY(window.m_hoverBar->isPanelVisible());
    QCOMPARE(window.m_hoverBarViewModel->zoomPercent(), 150);
    // The bar follows the larger pin.
    QTRY_VERIFY(window.frameGeometry().contains(window.m_hoverBar->barGeometry()));
    window.m_hoverBarViewModel->requestOpacityPercent(60);
    QCOMPARE(window.opacity(), 0.6);
}

void TestPinHoverIntegration::suppressedStates_data()
{
    QTest::addColumn<QString>("state");
    QTest::newRow("dragging") << "dragging";
    QTest::newRow("toolbar") << "toolbar";
    QTest::newRow("setting-off") << "setting-off";
    QTest::newRow("click-through") << "click-through";
}

void TestPinHoverIntegration::suppressedStates()
{
    QFETCH(QString, state);
    auto& settings = PinWindowSettingsManager::instance();
    const bool savedSetting = settings.loadShowHoverControls();
    auto restore = qScopeGuard([&] { settings.saveShowHoverControls(savedSetting); });
    if (state == "click-through" && !PlatformFeatures::instance().capabilities().supportsClickThrough)
        QSKIP("Click-through is not supported on this platform");
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    if (state == "setting-off") settings.saveShowHoverControls(false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(state == "setting-off" || window.m_hoverBar->isBarVisible(), 1500);
    if (state == "dragging") {
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(200, 150));
        QTest::mouseMove(&window, QPoint(230, 170));
    } else if (state == "toolbar") {
        window.showToolbar();
    } else if (state == "click-through") {
        window.setClickThrough(true);
    }
    QVERIFY(!window.m_hoverBar->isBarVisible());
    QTest::qWait(PinHoverBar::kShowDelayMs + 300);
    QVERIFY(!window.m_hoverBar->isBarVisible());
    if (state == "dragging") QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(230, 170));
}

void TestPinHoverIntegration::moreOpensContextMenu()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    window.m_hoverBarViewModel->requestMore();
    QVERIFY(window.m_contextMenu);
    QTRY_VERIFY(window.m_contextMenu->isVisible());
    window.m_contextMenu->hide();
}

void TestPinHoverIntegration::closingPinClosesBar()
{
    auto* window = new PinWindow(pixmap(), QPoint(150, 150), nullptr, false, false);
    window->setAttribute(Qt::WA_DeleteOnClose);
    hover(*window);
    QTRY_VERIFY_WITH_TIMEOUT(window->m_hoverBar->isBarVisible(), 1500);
    window->m_hoverBarViewModel->openPanel(PinHoverBarViewModel::ZoomPanel);
    QPointer<QWindow> bar = window->m_hoverBar->barWindow();
    QTRY_VERIFY(window->m_hoverBar->isPanelVisible());
    QPointer<QWindow> panel = window->m_hoverBar->panelWindow();
    window->close();
    QTRY_VERIFY(bar.isNull() || !bar->isVisible());
    QVERIFY(panel.isNull() || !panel->isVisible());
    // No event filter is left behind: a later key event is harmless.
    QWindow other;
    other.show();
    QVERIFY(QTest::qWaitForWindowExposed(&other));
    QTest::keyClick(&other, Qt::Key_Escape);
}

// Review #225-3: rotating changes the pin's size without a zoom change.
void TestPinHoverIntegration::rotateKeepsBarInside()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    window.rotateRight();
    QTRY_VERIFY(window.frameGeometry().size() == QSize(320, 480));
    QTRY_VERIFY(window.frameGeometry().contains(window.m_hoverBar->barGeometry()));
}

// Review #225-2: a click on the image hides the bar only while pressed.
void TestPinHoverIntegration::clickedBarComesBack()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
}

// Review #225-5: the Live hint belongs to the dot, not the whole image.
void TestPinHoverIntegration::liveTooltipOnlyOnDot()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    window.m_isLiveMode = true;
    QVERIFY(window.liveToolTipAt(QPoint(240, 160)).isEmpty());
    QVERIFY(!window.liveToolTipAt(QPoint(PinWindow::kLiveIndicatorMargin + 3, PinWindow::kLiveIndicatorMargin + 3)).isEmpty());
    window.m_isLiveMode = false;
    QVERIFY(window.liveToolTipAt(QPoint(PinWindow::kLiveIndicatorMargin + 3, PinWindow::kLiveIndicatorMargin + 3)).isEmpty());
    QVERIFY(window.toolTip().isEmpty());
}

void TestPinHoverIntegration::liveIndicatorTooltipIsExcluded_data()
{
    QTest::addColumn<bool>("prepareCapture");
    QTest::addColumn<bool>("showHoverControls");
    QTest::newRow("lazy") << false << true;
    QTest::newRow("prepared") << true << true;
    QTest::newRow("prepared-without-hover-bar") << true << false;
}

void TestPinHoverIntegration::liveIndicatorTooltipIsExcluded()
{
    QFETCH(bool, prepareCapture);
    QFETCH(bool, showHoverControls);
    auto& settings = PinWindowSettingsManager::instance();
    const bool savedSetting = settings.loadShowHoverControls();
    const auto restore = qScopeGuard([&] { settings.saveShowHoverControls(savedSetting); });
    settings.saveShowHoverControls(showHoverControls);
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    // The real pointer stays off the pin: a real Leave dismisses the hint.
    showWithoutRealHover(window);
    window.m_isLiveMode = true; // Exercise hints without starting a capture or requesting TCC.
    const QList<WId> excluded = prepareCapture ? window.liveCaptureExcludedWindowIds() : QList<WId>();
    const QPoint dot(PinWindow::kLiveIndicatorMargin + 3, PinWindow::kLiveIndicatorMargin + 3);
    QHelpEvent help(QEvent::ToolTip, dot, window.mapToGlobal(dot));
    QCoreApplication::sendEvent(&window, &help);
    QVERIFY2(!QToolTip::isVisible(), "Live hints must not create an unexcluded QWidget tooltip");

    QQuickView* tooltip = nullptr;
    for (QWindow* candidate : QGuiApplication::topLevelWindows()) {
        auto* view = qobject_cast<QQuickView*>(candidate);
        if (view && view->rootObject()
            && view->rootObject()->property("tooltipText").toString() == window.liveToolTipAt(dot)) {
            tooltip = view;
            break;
        }
    }
    QVERIFY(tooltip);
    QTRY_VERIFY(tooltip->isVisible() && tooltip->rootObject()->isVisible());
    const WId tooltipId = tooltip->winId();
    if (prepareCapture) QVERIFY(excluded.contains(tooltipId));
    QVERIFY(tooltip->flags().testFlag(Qt::WindowTransparentForInput));
    QVERIFY(tooltip->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
#ifdef Q_OS_WIN
    DWORD affinity = 0;
    QVERIFY(GetWindowDisplayAffinity(reinterpret_cast<HWND>(tooltipId), &affinity));
    QCOMPARE(affinity, DWORD(SnapTray::windowsCaptureAffinity(true, QOperatingSystemVersion::current())));
#endif

    const auto hidden = [&] { return !tooltip->isVisible() || !tooltip->rootObject()->isVisible(); };
    // Sent like the help and leave events: QTest::mouseMove without a button
    // does not reach the widget on every platform.
    const QPoint away(240, 160);
    QMouseEvent move(QEvent::MouseMove, away, window.mapToGlobal(away), Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &move);
    QTRY_VERIFY(hidden());
    QCoreApplication::sendEvent(&window, &help);
    QTRY_VERIFY(tooltip->isVisible() && tooltip->rootObject()->isVisible());
    QCOMPARE(tooltip->winId(), tooltipId);
    if (prepareCapture) QCOMPARE(window.liveCaptureExcludedWindowIds(), excluded);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&window, &leave);
    QTRY_VERIFY(hidden());

    // A queued show cannot resurrect the hint after Live stops or the pin hides.
    QCoreApplication::sendEvent(&window, &help);
    window.stopLiveCapture();
    QCoreApplication::processEvents();
    QVERIFY(hidden());
    window.m_isLiveMode = true;
    QCoreApplication::sendEvent(&window, &help);
    window.hide();
    QCoreApplication::processEvents();
    QVERIFY(hidden());
}

// PR #241 review: a live pin overlapping its source must not capture its own
// hover bar, value panel or either tooltip, even before the first hover.
void TestPinHoverIntegration::liveCaptureExcludesHoverWindows()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    showWithoutRealHover(window);
    const QList<WId> ids = window.liveCaptureExcludedWindowIds();
    QCOMPARE(ids.size(), 5);
    QVERIFY(ids.contains(window.winId()));
    QVERIFY(window.m_hoverBar);
    QVERIFY(window.m_hoverBar->barWindow());
    QVERIFY(window.m_hoverBar->panelWindow());
    QVERIFY(window.m_hoverBar->tooltipWindow());
    QVERIFY(ids.contains(window.m_hoverBar->barWindow()->winId()));
    QVERIFY(ids.contains(window.m_hoverBar->panelWindow()->winId()));
    QVERIFY(ids.contains(window.m_hoverBar->tooltipWindow()->winId()));
}

// PR #241 review: zooming from the slider resizes the pin, which moves the bar.
// The panel under the pointer stays put during the drag, so the slider keeps
// its input coordinates; it follows the bar again on release.
void TestPinHoverIntegration::sliderDragKeepsPanelInPlace()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    window.m_hoverBarViewModel->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(window.m_hoverBar->isPanelVisible());
    auto* panel = qobject_cast<QQuickView*>(window.m_hoverBar->panelWindow());
    QVERIFY(panel);
    QVERIFY(QTest::qWaitForWindowExposed(panel));
    auto* slider = panel->rootObject()->findChild<QQuickItem*>(QStringLiteral("pinValueSlider"));
    QVERIFY(slider);
    const QPoint start = slider->mapToScene(QPointF(slider->width() * 0.3, slider->height() / 2)).toPoint();
    const QPoint before = panel->position();

    QTest::mousePress(panel, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(panel, start + QPoint(10, 0));
    QTRY_VERIFY(window.zoomLevel() > 1.0);
    const qreal first = window.zoomLevel();
    QCOMPARE(panel->position(), before);
    QTest::mouseMove(panel, start + QPoint(20, 0));
    QTRY_VERIFY(window.zoomLevel() > first);
    QCOMPARE(panel->position(), before);
    QVERIFY(window.m_hoverBar->isPanelVisible());

    QTest::mouseRelease(panel, Qt::LeftButton, Qt::NoModifier, start + QPoint(20, 0));
    const QRect bar = window.m_hoverBar->barGeometry();
    QTRY_VERIFY(panel->geometry().bottom() < bar.top() || panel->geometry().top() > bar.bottom());
    QTRY_COMPARE(qAbs(panel->geometry().bottom() + 1 + PinHoverBar::kPanelGap - bar.top()) <= 1
                     || qAbs(bar.bottom() + 1 + PinHoverBar::kPanelGap - panel->geometry().top()) <= 1,
                 true);
}

// Windows CI (PR #241): a native window recreation hides and re-shows the
// pin at once, and the pin can sync the bar in between. Only a hide that
// lasts closes the bar and the panel.
void TestPinHoverIntegration::transientHideKeepsPanel()
{
    PinWindow window(pixmap(), QPoint(150, 150), nullptr, false, false);
    hover(window);
    QTRY_VERIFY_WITH_TIMEOUT(window.m_hoverBar->isBarVisible(), 1500);
    window.m_hoverBarViewModel->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(window.m_hoverBar->isPanelVisible());
    window.setVisible(false);
    window.syncHoverBar();
    window.setVisible(true);
    window.syncHoverBar();
    QTest::qWait(50);
    QVERIFY(window.m_hoverBar->isBarVisible());
    QVERIFY(window.m_hoverBar->isPanelVisible());
    // A lasting hide still closes both.
    window.hide();
    QTRY_VERIFY(!window.m_hoverBar->isBarVisible());
    QVERIFY(!window.m_hoverBar->isPanelVisible());
}

QTEST_MAIN(TestPinHoverIntegration)
#include "tst_PinHoverIntegration.moc"
