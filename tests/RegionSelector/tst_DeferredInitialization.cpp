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
    void testWindowListReady_afterRevealAppliesDeferredDetection();
    void testShowEvent_usesLiveCursorPositionForInitialReveal();
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

void tst_RegionSelectorDeferredInitialization::testWindowListReady_afterRevealAppliesDeferredDetection()
{
    QScreen* screen = currentCursorScreen();
    if (!screen) {
        QSKIP("No screens available for RegionSelector deferred detection test.");
    }

    WindowDetector detector;
    detector.m_currentScreen = screen;
    detector.m_refreshComplete = false;
    detector.m_cacheReady = false;
    detector.m_cacheScreen = nullptr;

    RegionSelector selector;
    selector.setWindowDetector(&detector);

    const QSize size = screen->geometry().size().boundedTo(QSize(320, 240));
    selector.initializeForScreen(screen, makePreCapture(size, Qt::darkBlue));
    selector.setGeometry(screen->geometry());
    RegionSelectorTestAccess::showForRevealTests(selector);

    QVERIFY(RegionSelectorTestAccess::initialRevealStateIsRevealed(selector));
    QVERIFY(!selector.m_inputState.hasDetectedWindow);
    QVERIFY(selector.m_inputState.highlightedWindowRect.isNull());

    DetectedElement element;
    element.bounds = screen->geometry();
    element.windowLayer = 0;
    element.windowId = 1;
    element.elementType = ElementType::Window;

    const QPoint originalCursorPos = QCursor::pos();
    const QPoint cursorPos = screen->geometry().center();
    QCursor::setPos(cursorPos);
    QCoreApplication::processEvents();

    detector.m_windowCache.clear();
    detector.m_windowCache.push_back(element);
    detector.m_cacheScreen = screen;
    detector.m_cacheReady = true;
    detector.m_cacheQueryMode = WindowDetector::QueryMode::IncludeChildControls;
    detector.m_refreshComplete = true;

    detector.windowListReady();
    QCoreApplication::processEvents();

    QCursor::setPos(originalCursorPos);
    QVERIFY(selector.m_inputState.hasDetectedWindow);
    QVERIFY(!selector.m_inputState.highlightedWindowRect.isNull());
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
