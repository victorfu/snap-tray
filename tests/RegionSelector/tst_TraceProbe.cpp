#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>
#include <QSignalSpy>

#include "RegionSelectorTestAccess.h"
#include "settings/RegionCaptureSettingsManager.h"
#include "region/CaptureChromeWindow.h"
#include "utils/CoordinateHelper.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class tst_RegionSelectorTraceProbe : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testInitializeForScreenRecordsCaptureContext();
    void testWindowDetectionRequestIsTraced();
    void testPaintRecordsDirtyRegionAndFloatingUiSnapshot();
    void testSwitchToScreenSameScreenDoesNotRecordAdditionalCaptureContext();
    void testHandleInitialRevealTimeoutRevealsVisibleSelector();
    void testCompletedSelectionHoverDoesNotTriggerHostPaint();
    void testDetectedWindowDragTransitionUsesDetachedChromeOrFullHostPaint();
    void testSelectionCompletionDefersFloatingUiUntilChromePaint();

private:
    bool m_originalMagnifierEnabled = RegionCaptureSettingsManager::kDefaultMagnifierEnabled;
};

void tst_RegionSelectorTraceProbe::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector trace probe tests.");
    }

    auto& settings = RegionCaptureSettingsManager::instance();
    m_originalMagnifierEnabled = settings.isMagnifierEnabled();
    settings.setMagnifierEnabled(true);
}

void tst_RegionSelectorTraceProbe::cleanupTestCase()
{
    RegionCaptureSettingsManager::instance().setMagnifierEnabled(m_originalMagnifierEnabled);
}

void tst_RegionSelectorTraceProbe::testInitializeForScreenRecordsCaptureContext()
{
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap preCapture(QSize(160, 120));
    preCapture.fill(Qt::darkCyan);

    selector.initializeForScreen(screen, preCapture);

    QVERIFY(!probe.captureContextEvents.isEmpty());
    const auto& record = probe.captureContextEvents.constLast();
    QCOMPARE(record.backgroundPixelSize, preCapture.size());
    QCOMPARE(record.backgroundLogicalSize,
             CoordinateHelper::toLogical(preCapture.size(), screen->devicePixelRatio()));
    QCOMPARE(record.devicePixelRatio, screen->devicePixelRatio());
    QVERIFY(record.hasSourceScreen);
}

void tst_RegionSelectorTraceProbe::testWindowDetectionRequestIsTraced()
{
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    RegionSelectorTestAccess::dispatchMouseMove(selector, QPoint(20, 20));

    QVERIFY(!probe.windowDetectionRequests.isEmpty());
    QCOMPARE(probe.windowDetectionRequests.constLast(), QPoint(20, 20));
}

void tst_RegionSelectorTraceProbe::testPaintRecordsDirtyRegionAndFloatingUiSnapshot()
{
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap preCapture(QSize(320, 240));
    preCapture.fill(Qt::black);
    selector.initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(20, 20, 120, 80));

    const QRegion dirtyRegion(QRect(0, 0, 180, 120));
    RegionSelectorTestAccess::invokePaint(selector, dirtyRegion);

    QVERIFY(!probe.paintEvents.isEmpty());
    QCOMPARE(probe.paintEvents.constLast().boundingRect, dirtyRegion.boundingRect());

    QVERIFY(!probe.floatingUiSnapshots.isEmpty());
    const auto& snapshot = probe.floatingUiSnapshots.constLast();
    QCOMPARE(snapshot.toolbarVisible, RegionSelectorTestAccess::toolbarVisible(selector));
    QCOMPARE(snapshot.toolbarGeometry, RegionSelectorTestAccess::toolbarGeometry(selector));
    QCOMPARE(snapshot.subToolbarVisible, RegionSelectorTestAccess::subToolbarVisible(selector));
    QCOMPARE(snapshot.regionControlVisible,
             RegionSelectorTestAccess::regionControlVisible(selector));
    QCOMPARE(snapshot.regionControlGeometry,
             RegionSelectorTestAccess::regionControlGeometry(selector));
    QCOMPARE(snapshot.magnifierVisible, RegionSelectorTestAccess::magnifierVisible(selector));
    QCOMPARE(snapshot.magnifierGeometry, RegionSelectorTestAccess::magnifierGeometry(selector));
}

void tst_RegionSelectorTraceProbe::testSwitchToScreenSameScreenDoesNotRecordAdditionalCaptureContext()
{
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap preCapture(QSize(200, 120));
    preCapture.fill(Qt::darkBlue);
    selector.initializeForScreen(screen, preCapture);

    const int countBefore = RegionSelectorTestAccess::captureContextEventCount(probe);
    selector.switchToScreen(screen, true);
    QCOMPARE(RegionSelectorTestAccess::captureContextEventCount(probe), countBefore);
}

void tst_RegionSelectorTraceProbe::testHandleInitialRevealTimeoutRevealsVisibleSelector()
{
    RegionSelector selector;

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap preCapture(QSize(200, 120));
    preCapture.fill(Qt::darkGreen);
    selector.initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    RegionSelectorTestAccess::markInitialRevealPreparing(selector);
    selector.setWindowOpacity(0.0);

    RegionSelectorTestAccess::invokeHandleInitialRevealTimeout(selector);

    QVERIFY(RegionSelectorTestAccess::initialRevealStateIsRevealed(selector));
    QCOMPARE(selector.windowOpacity(), 1.0);
}

void tst_RegionSelectorTraceProbe::testCompletedSelectionHoverDoesNotTriggerHostPaint()
{
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap preCapture(QSize(320, 240));
    preCapture.fill(Qt::black);
    selector.initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
    RegionSelectorTestAccess::setSelectionRect(selector, QRect(30, 30, 140, 90));

    RegionSelectorTestAccess::dispatchMouseMove(selector, QPoint(76, 76));
    QCoreApplication::processEvents();
    probe.paintEvents.clear();

    QTest::qWait(40);
    RegionSelectorTestAccess::dispatchMouseMove(selector, QPoint(80, 80));
    QCoreApplication::processEvents();
    QTest::qWait(40);
    QCoreApplication::processEvents();

    QCOMPARE(probe.paintEvents.size(), 0);
}

void tst_RegionSelectorTraceProbe::testDetectedWindowDragTransitionUsesDetachedChromeOrFullHostPaint()
{
#ifndef Q_OS_WIN
    QSKIP("Windows-specific repaint transition regression.");
#else
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap preCapture(QSize(320, 240));
    preCapture.fill(Qt::black);
    selector.initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
    const bool detachedCaptureWindows = RegionSelectorTestAccess::usesDetachedCaptureWindows(selector);

    RegionSelectorTestAccess::dispatchMouseMove(selector, QPoint(8, 8));
    QCoreApplication::processEvents();

    RegionSelectorTestAccess::seedDetectedWindowHighlight(selector, QRect(30, 20, 220, 120));
    if (!detachedCaptureWindows) {
        RegionSelectorTestAccess::invokePaint(selector, QRegion(selector.rect()));
    }
    probe.paintEvents.clear();

    RegionSelectorTestAccess::dispatchMousePress(selector, QPoint(60, 60));
    RegionSelectorTestAccess::dispatchMouseMove(selector, QPoint(170, 140));
    QCoreApplication::processEvents();

    if (detachedCaptureWindows) {
        QTRY_VERIFY(RegionSelectorTestAccess::captureChromeVisible(selector));
        QCOMPARE(probe.paintEvents.size(), 0);
        return;
    }

    QTRY_VERIFY(!probe.paintEvents.isEmpty());

    bool sawFullWidgetPaint = false;
    for (const auto& record : probe.paintEvents) {
        if (record.boundingRect == selector.rect()) {
            sawFullWidgetPaint = true;
            break;
        }
    }
    QVERIFY(sawFullWidgetPaint);
#endif
}

void tst_RegionSelectorTraceProbe::testSelectionCompletionDefersFloatingUiUntilChromePaint()
{
    RegionSelector selector;
    RegionSelectorTestAccess::TraceProbe probe;
    RegionSelectorTestAccess::attachTraceProbe(selector, &probe);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap preCapture(QSize(320, 240));
    preCapture.fill(Qt::black);
    selector.initializeForScreen(screen, preCapture);
    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();

    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
    const bool detachedCaptureWindows = RegionSelectorTestAccess::usesDetachedCaptureWindows(selector);

    RegionSelectorTestAccess::dispatchMousePress(selector, QPoint(40, 40));
    RegionSelectorTestAccess::dispatchMouseMove(selector, QPoint(180, 140));
    QCoreApplication::processEvents();

    if (!detachedCaptureWindows) {
        QSKIP("Detached chrome handoff only applies on Windows detached capture.");
    }

    auto* chrome = RegionSelectorTestAccess::captureChromeWindow(selector);
    QVERIFY(chrome);
    QSignalSpy paintSpy(chrome, SIGNAL(framePainted()));
    QTRY_VERIFY(RegionSelectorTestAccess::captureChromeVisible(selector));

    probe.paintEvents.clear();
    RegionSelectorTestAccess::dispatchMouseRelease(selector, QPoint(180, 140));

    QVERIFY(selector.isSelectionComplete());
    QVERIFY(!RegionSelectorTestAccess::toolbarVisible(selector));
    QVERIFY(!RegionSelectorTestAccess::regionControlVisible(selector));
    QCOMPARE(probe.paintEvents.size(), 0);

    QTRY_VERIFY(!paintSpy.isEmpty());
    QTRY_VERIFY(RegionSelectorTestAccess::toolbarVisible(selector));
}

QTEST_MAIN(tst_RegionSelectorTraceProbe)
#include "tst_TraceProbe.moc"
