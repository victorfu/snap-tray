#include <QtTest/QtTest>

#include "pinwindow/PinWindowPlacement.h"

class TestPinWindowPlacement : public QObject
{
    Q_OBJECT

private slots:
    void testPreservesRelativePositionAcrossScreens();
    void testEvenSizedWindowRoundTripPreservesTopLeft();
    void testSourceCenterOutsideBoundsClampsBeforeMapping();
    void testWindowLargerThanTargetAnchorsToTopLeft();
    void testNegativeCoordinateScreenLayoutMapping();
    void testInvalidSourceGeometryFallsBackToTargetCenter();
};

void TestPinWindowPlacement::testPreservesRelativePositionAcrossScreens()
{
    const QRect windowFrame(100, 120, 201, 101);
    const QRect sourceAvailable(0, 0, 1000, 800);
    const QRect targetAvailable(2000, 100, 1000, 800);

    const QPoint topLeft = computePinWindowTopLeftForTargetScreen(
        windowFrame,
        sourceAvailable,
        targetAvailable);

    QCOMPARE(topLeft, QPoint(2100, 220));
}

void TestPinWindowPlacement::testEvenSizedWindowRoundTripPreservesTopLeft()
{
    const QRect windowFrame(100, 120, 200, 100);
    const QRect sourceAvailable(0, 0, 1000, 800);
    const QRect targetAvailable(2000, 100, 1000, 800);

    const QPoint mappedTopLeft = computePinWindowTopLeftForTargetScreen(
        windowFrame,
        sourceAvailable,
        targetAvailable);
    const QRect mappedFrame(mappedTopLeft, windowFrame.size());

    const QPoint roundTripTopLeft = computePinWindowTopLeftForTargetScreen(
        mappedFrame,
        targetAvailable,
        sourceAvailable);

    QCOMPARE(roundTripTopLeft, windowFrame.topLeft());
}

void TestPinWindowPlacement::testSourceCenterOutsideBoundsClampsBeforeMapping()
{
    const QRect windowFrame(1200, 900, 201, 101);
    const QRect sourceAvailable(0, 0, 1000, 800);
    const QRect targetAvailable(-1500, -200, 1200, 700);

    const QPoint topLeft = computePinWindowTopLeftForTargetScreen(
        windowFrame,
        sourceAvailable,
        targetAvailable);

    QCOMPARE(topLeft, QPoint(-501, 399));
}

void TestPinWindowPlacement::testWindowLargerThanTargetAnchorsToTopLeft()
{
    const QRect windowFrame(400, 300, 1600, 900);
    const QRect sourceAvailable(0, 0, 1920, 1080);
    const QRect targetAvailable(-300, 200, 1200, 700);

    const QPoint topLeft = computePinWindowTopLeftForTargetScreen(
        windowFrame,
        sourceAvailable,
        targetAvailable);

    QCOMPARE(topLeft, targetAvailable.topLeft());
}

void TestPinWindowPlacement::testNegativeCoordinateScreenLayoutMapping()
{
    const QRect windowFrame(2500, 300, 301, 201);
    const QRect sourceAvailable(1920, 0, 1920, 1080);
    const QRect targetAvailable(-1280, -300, 1280, 1024);

    const QPoint topLeft = computePinWindowTopLeftForTargetScreen(
        windowFrame,
        sourceAvailable,
        targetAvailable);

    QCOMPARE(topLeft, QPoint(-943, -21));
}

void TestPinWindowPlacement::testInvalidSourceGeometryFallsBackToTargetCenter()
{
    const QRect windowFrame(42, 17, 301, 201);
    const QRect sourceAvailable;
    const QRect targetAvailable(500, 200, 800, 600);

    const QPoint topLeft = computePinWindowTopLeftForTargetScreen(
        windowFrame,
        sourceAvailable,
        targetAvailable);

    QCOMPARE(topLeft, QPoint(750, 400));
}

QTEST_MAIN(TestPinWindowPlacement)
#include "tst_PinWindowPlacement.moc"
