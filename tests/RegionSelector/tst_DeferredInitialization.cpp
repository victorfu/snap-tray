#include <QtTest/QtTest>

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"
#include "WindowDetector.h"

namespace {

QScreen* currentCursorScreen()
{
    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos())) {
        return screen;
    }
    return QGuiApplication::primaryScreen();
}

QPixmap makePreCapture(const QSize& size, const QColor& color)
{
    QPixmap preCapture(size);
    preCapture.fill(color);
    return preCapture;
}

DetectedElement makeDetectedElement(
    const QRect& bounds,
    ElementType elementType,
    int windowLayer = 100)
{
    DetectedElement element;
    element.bounds = bounds;
    element.windowTitle = QStringLiteral("test");
    element.ownerApp = QStringLiteral("DeferredInitTests");
    element.windowLayer = windowLayer;
    element.windowId = static_cast<uint32_t>(windowLayer + 1);
    element.elementType = elementType;
    element.ownerPid = 4242;
    return element;
}

QPoint currentLocalCursorPos(const QRect& screenGeometry)
{
    return QCursor::pos() - screenGeometry.topLeft();
}

}  // namespace

class tst_RegionSelectorDeferredInitialization : public QObject
{
    Q_OBJECT

private slots:
    void testShowEvent_revealsImmediatelyWithoutDetectorCache();
    void testShowEvent_usesLiveCursorPositionForInitialReveal();
    void testInitialReveal_skipsChildRefreshForStatusBarSurface();
    void testWindowDetection_restartsChildRefreshAfterLeavingStatusBarSurface();
    void testDetectedWindowClickOnDetachedWindowsArmsSelectionCompletionHandoff();
    void testDetectedWindowClickOnMacKeepsImmediateToolbarBehavior();

};

void tst_RegionSelectorDeferredInitialization::testShowEvent_revealsImmediatelyWithoutDetectorCache()
{
    QScreen* screen = currentCursorScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector deferred reveal test.");
    }

    WindowDetector detector;
    detector.m_currentScreen = screen;
    detector.m_refreshComplete = false;
    detector.m_cacheReady = false;
    detector.m_cacheScreen = nullptr;

    RegionSelector selector;
    selector.setWindowDetector(&detector);

    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    selector.initializeForScreen(screen, makePreCapture(size, Qt::darkCyan));
    selector.setGeometry(screen->geometry());

    QCOMPARE(selector.m_initialRevealState, RegionSelector::InitialRevealState::ReadyToReveal);

    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    QVERIFY(RegionSelectorTestAccess::initialRevealStateIsRevealed(selector));
    QVERIFY(qFuzzyCompare(selector.windowOpacity(), 1.0));
}

void tst_RegionSelectorDeferredInitialization::testShowEvent_usesLiveCursorPositionForInitialReveal()
{
    QScreen* screen = currentCursorScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector live-cursor reveal test.");
    }

    const QPoint originalCursorPos = QCursor::pos();
    const QRect screenGeometry = screen->geometry();
    const QPoint targetCursorPos(
        qBound(screenGeometry.left(), screenGeometry.center().x() + 40, screenGeometry.right()),
        qBound(screenGeometry.top(), screenGeometry.center().y() + 30, screenGeometry.bottom()));

    WindowDetector detector;
    detector.m_currentScreen = screen;
    detector.m_refreshComplete = false;
    detector.m_cacheReady = false;
    detector.m_cacheScreen = nullptr;

    RegionSelector selector;
    selector.setWindowDetector(&detector);

    const QSize size = screenGeometry.size().boundedTo(QSize(320, 240));
    selector.initializeForScreen(screen, makePreCapture(size, Qt::darkMagenta));
    selector.setGeometry(screenGeometry);

    QCursor::setPos(targetCursorPos);
    QCoreApplication::processEvents();
    if (QCursor::pos() != targetCursorPos) {
        QCursor::setPos(originalCursorPos);
        QSKIP("System cursor position could not be adjusted for reveal test.");
    }

    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    const QPoint expectedLocalPoint = currentLocalCursorPos(screenGeometry);
    QCursor::setPos(originalCursorPos);
    QCOMPARE(selector.m_inputState.currentPoint, expectedLocalPoint);
}

void tst_RegionSelectorDeferredInitialization::testInitialReveal_skipsChildRefreshForStatusBarSurface()
{
    QScreen* screen = currentCursorScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector status bar refinement test.");
    }

    WindowDetector detector;
    detector.m_enabled = true;
    detector.setScreen(screen);
    detector.m_refreshComplete = true;
    detector.m_cacheReady = true;
    detector.m_cacheScreen = screen;
    detector.m_cacheQueryMode = WindowDetector::QueryMode::TopLevelOnly;

    const QRect screenGeometry = screen->geometry();
    const QPoint cursorPos = QCursor::pos();
    const QRect statusBarBounds(
        qBound(screenGeometry.left(), cursorPos.x() - 120, screenGeometry.right() - 240),
        qBound(screenGeometry.top(), cursorPos.y() - 60, screenGeometry.bottom() - 120),
        240,
        120);
    detector.m_windowCache = {
        makeDetectedElement(statusBarBounds, ElementType::StatusBarItem, 101)
    };

    RegionSelector selector;
    selector.setWindowDetector(&detector);
    selector.initializeForScreen(
        screen,
        makePreCapture(screenGeometry.size().boundedTo(QSize(320, 240)), Qt::darkBlue));
    selector.setGeometry(screenGeometry);

    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    QVERIFY(selector.m_detectedWindow.has_value());
    QCOMPARE(selector.m_detectedWindow->elementType, ElementType::StatusBarItem);
    QCOMPARE(selector.m_detectedWindow->bounds, statusBarBounds);
    QCOMPARE(selector.m_inputState.highlightedWindowRect,
             QRect(statusBarBounds.topLeft() - screenGeometry.topLeft(), statusBarBounds.size()));
    QCOMPARE(detector.m_refreshRequestId.load(), uint64_t(0));
    QCOMPARE(detector.m_cacheQueryMode, WindowDetector::QueryMode::TopLevelOnly);
}

void tst_RegionSelectorDeferredInitialization::
testWindowDetection_restartsChildRefreshAfterLeavingStatusBarSurface()
{
    QScreen* screen = currentCursorScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector status bar refinement exit test.");
    }

    WindowDetector detector;
    detector.m_enabled = true;
    detector.setScreen(screen);
    detector.m_refreshComplete = true;
    detector.m_cacheReady = true;
    detector.m_cacheScreen = screen;
    detector.m_cacheQueryMode = WindowDetector::QueryMode::TopLevelOnly;

    const QRect screenGeometry = screen->geometry();
    const QPoint cursorPos = QCursor::pos();
    const QRect statusBarBounds(
        qBound(screenGeometry.left(), cursorPos.x() - 120, screenGeometry.right() - 240),
        qBound(screenGeometry.top(), cursorPos.y() - 60, screenGeometry.bottom() - 120),
        240,
        120);
    detector.m_windowCache = {
        makeDetectedElement(statusBarBounds, ElementType::StatusBarItem, 101)
    };

    RegionSelector selector;
    selector.setWindowDetector(&detector);
    selector.initializeForScreen(
        screen,
        makePreCapture(screenGeometry.size().boundedTo(QSize(320, 240)), Qt::darkRed));
    selector.setGeometry(screenGeometry);

    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    const QPoint outsideLocalPoint(
        qBound(0, statusBarBounds.right() - screenGeometry.left() + 40, selector.width() - 1),
        qBound(0, statusBarBounds.bottom() - screenGeometry.top() + 40, selector.height() - 1));

    RegionSelectorTestAccess::dispatchMouseMove(selector, outsideLocalPoint);
    QCoreApplication::processEvents();

    QVERIFY(detector.m_refreshRequestId.load() > 0);
}

void tst_RegionSelectorDeferredInitialization::
testDetectedWindowClickOnDetachedWindowsArmsSelectionCompletionHandoff()
{
#ifndef Q_OS_WIN
    QSKIP("Detached capture window handoff regression only applies on Windows.");
#else
    QScreen* screen = currentCursorScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector detected-window handoff test.");
    }

    RegionSelector selector;
    const QRect screenGeometry = screen->geometry();
    const QSize size = screenGeometry.size().boundedTo(QSize(320, 240));
    selector.initializeForScreen(screen, makePreCapture(size, Qt::darkGreen));
    selector.setGeometry(screenGeometry);
    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    if (!RegionSelectorTestAccess::usesDetachedCaptureWindows(selector)) {
        QSKIP("Detached capture windows are not active in this environment.");
    }

    const QRect detectedRect(40, 40, 160, 120);
    const QPoint clickPos = detectedRect.center();
    RegionSelectorTestAccess::seedDetectedWindowHighlight(selector, detectedRect);
    RegionSelectorTestAccess::dispatchMousePress(selector, clickPos);
    RegionSelectorTestAccess::dispatchMouseRelease(selector, clickPos);

    QVERIFY(RegionSelectorTestAccess::selectionCompletionHandoffPending(selector));
    QCoreApplication::processEvents();

    QVERIFY(selector.m_selectionManager->isComplete());
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), detectedRect);
    QTRY_VERIFY_WITH_TIMEOUT(
        RegionSelectorTestAccess::toolbarVisible(selector) ||
        RegionSelectorTestAccess::toolbarWindow(selector) == nullptr,
        1000);

    if (!RegionSelectorTestAccess::toolbarVisible(selector) &&
        !RegionSelectorTestAccess::toolbarWindow(selector)) {
        QSKIP("Floating toolbar window did not initialize in this environment.");
    }
    QVERIFY(RegionSelectorTestAccess::toolbarVisible(selector));
#endif
}

void tst_RegionSelectorDeferredInitialization::
testDetectedWindowClickOnMacKeepsImmediateToolbarBehavior()
{
#ifndef Q_OS_MACOS
    QSKIP("Inline detected-window toolbar regression only applies to macOS.");
#else
    QScreen* screen = currentCursorScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector macOS toolbar test.");
    }

    RegionSelector selector;
    const QRect screenGeometry = screen->geometry();
    const QSize size = screenGeometry.size().boundedTo(QSize(320, 240));
    selector.initializeForScreen(screen, makePreCapture(size, Qt::darkYellow));
    selector.setGeometry(screenGeometry);
    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    QVERIFY(!RegionSelectorTestAccess::usesDetachedCaptureWindows(selector));

    const QRect detectedRect(40, 40, 160, 120);
    const QPoint clickPos = detectedRect.center();
    RegionSelectorTestAccess::seedDetectedWindowHighlight(selector, detectedRect);
    RegionSelectorTestAccess::dispatchMousePress(selector, clickPos);
    RegionSelectorTestAccess::dispatchMouseRelease(selector, clickPos);
    QCoreApplication::processEvents();

    QVERIFY(selector.m_selectionManager->isComplete());
    QCOMPARE(RegionSelectorTestAccess::selectionRect(selector), detectedRect);
    QTRY_VERIFY_WITH_TIMEOUT(
        RegionSelectorTestAccess::toolbarVisible(selector) ||
        RegionSelectorTestAccess::toolbarWindow(selector) == nullptr,
        1000);

    if (!RegionSelectorTestAccess::toolbarVisible(selector) &&
        !RegionSelectorTestAccess::toolbarWindow(selector)) {
        QSKIP("Floating toolbar window did not initialize in this environment.");
    }
    QVERIFY(RegionSelectorTestAccess::toolbarVisible(selector));
#endif
}


QTEST_MAIN(tst_RegionSelectorDeferredInitialization)
#include "tst_DeferredInitialization.moc"
