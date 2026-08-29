#include <QtTest/QtTest>

#include "region/CaptureChromeWindow.h"

class tst_CaptureChromeDirtyRegionPlanner : public QObject
{
    Q_OBJECT

private slots:
    void testToolPreviewReasonOnlyClassifiesBoundedPencil();
    void testDirtyReasonRoutesToDistinctBuckets();
    void testConservativePreviewOnlyUsesPartialRegion();
    void testConservativeSceneDirtyForcesFullRegion();
    void testConservativeRenderStateChangeForcesFullRegion();
    void testConservativeStateDirtyForcesFullRegion();
    void testForcedRepaintUsesFullRegion();
    void testStableStateWithoutDirtyRegionSkipsRepaint();
    void testNonConservativePlanCombinesDirtyRegions();
    void testCurrentPlatformPolicyIsWired();
};

namespace {

snaptray::region::CaptureChromeDirtyRegionPlanInput baseInput()
{
    snaptray::region::CaptureChromeDirtyRegionPlanInput input;
    input.windowRect = QRect(0, 0, 640, 480);
    return input;
}

} // namespace

void tst_CaptureChromeDirtyRegionPlanner::testToolPreviewReasonOnlyClassifiesBoundedPencil()
{
    const QRegion preview(QRect(20, 30, 40, 50));

    QCOMPARE(
        snaptray::region::captureChromeDirtyReasonForToolPreview(true, preview),
        snaptray::region::CaptureChromeDirtyReason::PencilPreview);
    QCOMPARE(
        snaptray::region::captureChromeDirtyReasonForToolPreview(false, preview),
        snaptray::region::CaptureChromeDirtyReason::Scene);
    QCOMPARE(
        snaptray::region::captureChromeDirtyReasonForToolPreview(true, QRegion()),
        snaptray::region::CaptureChromeDirtyReason::Scene);
}

void tst_CaptureChromeDirtyRegionPlanner::testDirtyReasonRoutesToDistinctBuckets()
{
    snaptray::region::CaptureChromePendingDirtyRegions pending;
    const QRegion scene(QRect(10, 20, 30, 40));
    const QRegion preview(QRect(100, 110, 20, 30));
    const QRegion secondPreview(QRect(130, 140, 15, 25));

    pending.add(scene, snaptray::region::CaptureChromeDirtyReason::Scene);
    pending.add(preview, snaptray::region::CaptureChromeDirtyReason::PencilPreview);
    pending.add(secondPreview, snaptray::region::CaptureChromeDirtyReason::PencilPreview);

    QCOMPARE(pending.scene, scene);
    QRegion expectedPreview = preview;
    expectedPreview += secondPreview;
    QCOMPARE(pending.pencilPreview, expectedPreview);

    const auto taken = pending.take();
    QCOMPARE(taken.scene, scene);
    QCOMPARE(taken.pencilPreview, expectedPreview);
    QVERIFY(pending.scene.isEmpty());
    QVERIFY(pending.pencilPreview.isEmpty());
}

void tst_CaptureChromeDirtyRegionPlanner::testConservativePreviewOnlyUsesPartialRegion()
{
    auto input = baseInput();
    input.conservativeStateCompositing = true;
    input.previewDirtyRegion = QRegion(QRect(610, 450, 60, 60));

    const QRegion actual = snaptray::region::planCaptureChromeDirtyRegion(input);
    QCOMPARE(actual, QRegion(QRect(610, 450, 30, 30)));
}

void tst_CaptureChromeDirtyRegionPlanner::testConservativeSceneDirtyForcesFullRegion()
{
    auto input = baseInput();
    input.conservativeStateCompositing = true;
    input.previewDirtyRegion = QRegion(QRect(20, 30, 40, 50));
    input.sceneDirtyRegion = QRegion(QRect(100, 120, 10, 10));

    const QRegion actual = snaptray::region::planCaptureChromeDirtyRegion(input);
    QCOMPARE(actual, QRegion(input.windowRect));
}

void tst_CaptureChromeDirtyRegionPlanner::testConservativeRenderStateChangeForcesFullRegion()
{
    auto input = baseInput();
    input.conservativeStateCompositing = true;
    input.previewDirtyRegion = QRegion(QRect(20, 30, 40, 50));
    input.renderStateChanged = true;

    const QRegion actual = snaptray::region::planCaptureChromeDirtyRegion(input);
    QCOMPARE(actual, QRegion(input.windowRect));
}

void tst_CaptureChromeDirtyRegionPlanner::testConservativeStateDirtyForcesFullRegion()
{
    auto input = baseInput();
    input.conservativeStateCompositing = true;
    input.previewDirtyRegion = QRegion(QRect(20, 30, 40, 50));
    input.stateDirtyRegion = QRegion(QRect(90, 100, 20, 20));

    const QRegion actual = snaptray::region::planCaptureChromeDirtyRegion(input);
    QCOMPARE(actual, QRegion(input.windowRect));
}

void tst_CaptureChromeDirtyRegionPlanner::testForcedRepaintUsesFullRegion()
{
    auto input = baseInput();
    input.previewDirtyRegion = QRegion(QRect(20, 30, 40, 50));
    input.forceFullRepaint = true;

    const QRegion actual = snaptray::region::planCaptureChromeDirtyRegion(input);
    QCOMPARE(actual, QRegion(input.windowRect));
}

void tst_CaptureChromeDirtyRegionPlanner::testStableStateWithoutDirtyRegionSkipsRepaint()
{
    auto input = baseInput();
    input.conservativeStateCompositing = true;

    const QRegion actual = snaptray::region::planCaptureChromeDirtyRegion(input);
    QVERIFY(actual.isEmpty());
}

void tst_CaptureChromeDirtyRegionPlanner::testNonConservativePlanCombinesDirtyRegions()
{
    auto input = baseInput();
    input.sceneDirtyRegion = QRegion(QRect(10, 20, 30, 40));
    input.previewDirtyRegion = QRegion(QRect(100, 110, 20, 30));
    input.stateDirtyRegion = QRegion(QRect(620, 460, 40, 40));

    QRegion expected = input.sceneDirtyRegion;
    expected += input.previewDirtyRegion;
    expected += input.stateDirtyRegion;
    expected = expected.intersected(QRegion(input.windowRect));

    const QRegion actual = snaptray::region::planCaptureChromeDirtyRegion(input);
    QCOMPARE(actual, expected);
}

void tst_CaptureChromeDirtyRegionPlanner::testCurrentPlatformPolicyIsWired()
{
    auto input = baseInput();
    input.previewDirtyRegion = QRegion(QRect(20, 30, 40, 50));
    input.sceneDirtyRegion = QRegion(QRect(100, 110, 20, 30));

    const QRegion actual =
        snaptray::region::planCaptureChromeDirtyRegionForCurrentPlatform(input);
#ifdef Q_OS_WIN
    QCOMPARE(actual, QRegion(input.windowRect));
#else
    QRegion expected = input.previewDirtyRegion;
    expected += input.sceneDirtyRegion;
    QCOMPARE(actual, expected);
#endif
}

QTEST_APPLESS_MAIN(tst_CaptureChromeDirtyRegionPlanner)
#include "tst_CaptureChromeDirtyRegionPlanner.moc"
