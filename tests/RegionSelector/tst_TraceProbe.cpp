#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QPaintEvent>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlOverlayPanel.h"
#include "region/RegionInputHandler.h"
#include "region/MagnifierOverlay.h"
#include "region/SelectionStateManager.h"
#include "utils/CoordinateHelper.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class tst_RegionSelectorTraceProbe : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testInitializeForScreenRecordsCaptureContext();
    void testWindowDetectionRequestIsTraced();
    void testPaintRecordsDirtyRegionAndFloatingUiSnapshot();
};

void tst_RegionSelectorTraceProbe::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector trace probe tests.");
    }
}

void tst_RegionSelectorTraceProbe::testInitializeForScreenRecordsCaptureContext()
{
    RegionSelector selector;
    RegionSelector::RegionSelectorTraceProbe probe;
    selector.m_traceProbe = &probe;

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
    RegionSelector::RegionSelectorTraceProbe probe;
    selector.m_traceProbe = &probe;

    QMouseEvent moveEvent(QEvent::MouseMove,
                          QPointF(20, 20),
                          QPointF(20, 20),
                          Qt::NoButton,
                          Qt::NoButton,
                          Qt::NoModifier);
    selector.m_inputHandler->handleMouseMove(&moveEvent);

    QVERIFY(!probe.windowDetectionRequests.isEmpty());
    QCOMPARE(probe.windowDetectionRequests.constLast(), QPoint(20, 20));
}

void tst_RegionSelectorTraceProbe::testPaintRecordsDirtyRegionAndFloatingUiSnapshot()
{
    RegionSelector selector;
    RegionSelector::RegionSelectorTraceProbe probe;
    selector.m_traceProbe = &probe;

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    QPixmap preCapture(QSize(320, 240));
    preCapture.fill(Qt::black);
    selector.initializeForScreen(screen, preCapture);
    selector.m_initialRevealState = RegionSelector::InitialRevealState::Revealed;
    selector.m_selectionManager->setSelectionRect(QRect(20, 20, 120, 80));

    const QRegion dirtyRegion(QRect(0, 0, 180, 120));
    QPaintEvent event(dirtyRegion);
    selector.paintEvent(&event);

    QVERIFY(!probe.paintEvents.isEmpty());
    QCOMPARE(probe.paintEvents.constLast().boundingRect, dirtyRegion.boundingRect());

    QVERIFY(!probe.floatingUiSnapshots.isEmpty());
    const auto& snapshot = probe.floatingUiSnapshots.constLast();
    QCOMPARE(snapshot.toolbarVisible, selector.m_qmlToolbar && selector.m_qmlToolbar->isVisible());
    QCOMPARE(snapshot.toolbarGeometry, selector.m_qmlToolbar ? selector.m_qmlToolbar->geometry() : QRect());
    QCOMPARE(snapshot.subToolbarVisible,
             selector.m_qmlSubToolbar && selector.m_qmlSubToolbar->isVisible());
    QCOMPARE(snapshot.regionControlVisible,
             selector.m_regionControlPanel && selector.m_regionControlPanel->isVisible());
    QCOMPARE(snapshot.regionControlGeometry,
             selector.m_regionControlPanel ? selector.m_regionControlPanel->geometry() : QRect());
    QCOMPARE(snapshot.magnifierVisible,
             selector.m_magnifierOverlay && selector.m_magnifierOverlay->isVisible());
    QCOMPARE(snapshot.magnifierGeometry,
             selector.m_magnifierOverlay ? selector.m_magnifierOverlay->geometry() : QRect());
}

QTEST_MAIN(tst_RegionSelectorTraceProbe)
#include "tst_TraceProbe.moc"
