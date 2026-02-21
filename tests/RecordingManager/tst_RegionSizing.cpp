#include <QtTest/QtTest>

#include "RecordingRegionNormalizer.h"
#include "utils/CoordinateHelper.h"

class TestRecordingRegionSizing : public QObject
{
    Q_OBJECT

private slots:
    void testAlreadyEvenPhysicalRegionUnchanged();
    void testOddRegionExpandsRightAndBottom();
    void testOddRegionShiftsWhenAtRightBottomBoundary();
    void testFractionalDprUsesTwoSidedExpansionBeforeCrop();
    void testFullScreenOddRegionFallsBackToCrop();
    void testFullScreenOddRegionCropsMoreThanOneStepAtFractionalDpr();
    void testHighDpiNormalizationAt15();
    void testHighDpiNormalizationAt11();
};

namespace {
void verifyEvenPhysicalSize(const QRect& region, qreal dpr)
{
    const QSize physicalSize = CoordinateHelper::toPhysical(region.size(), dpr);
    QVERIFY2((physicalSize.width() % 2) == 0, qPrintable(QString("Expected even physical width, got %1").arg(physicalSize.width())));
    QVERIFY2((physicalSize.height() % 2) == 0, qPrintable(QString("Expected even physical height, got %1").arg(physicalSize.height())));
}
} // namespace

void TestRecordingRegionSizing::testAlreadyEvenPhysicalRegionUnchanged()
{
    const QRect bounds(0, 0, 400, 300);
    const QRect originalRegion(10, 20, 100, 50);

    const QRect normalized = normalizeToEvenPhysicalRegion(originalRegion, bounds, 1.0);

    QCOMPARE(normalized, originalRegion);
    QVERIFY(bounds.contains(normalized));
    verifyEvenPhysicalSize(normalized, 1.0);
}

void TestRecordingRegionSizing::testOddRegionExpandsRightAndBottom()
{
    const QRect bounds(0, 0, 400, 300);
    const QRect originalRegion(10, 10, 101, 51);

    const QRect normalized = normalizeToEvenPhysicalRegion(originalRegion, bounds, 1.0);

    QCOMPARE(normalized, QRect(10, 10, 102, 52));
    QVERIFY(bounds.contains(normalized));
    verifyEvenPhysicalSize(normalized, 1.0);
}

void TestRecordingRegionSizing::testOddRegionShiftsWhenAtRightBottomBoundary()
{
    const QRect bounds(0, 0, 400, 300);
    const QRect originalRegion(299, 249, 101, 51); // right/bottom already touch bounds

    const QRect normalized = normalizeToEvenPhysicalRegion(originalRegion, bounds, 1.0);

    QCOMPARE(normalized, QRect(298, 248, 102, 52));
    QVERIFY(bounds.contains(normalized));
    verifyEvenPhysicalSize(normalized, 1.0);
}

void TestRecordingRegionSizing::testFractionalDprUsesTwoSidedExpansionBeforeCrop()
{
    const QRect bounds(0, 0, 16, 16);
    const QRect originalRegion(1, 1, 14, 14); // odd physical size at DPR 1.1
    constexpr qreal dpr = 1.1;

    const QRect normalized = normalizeToEvenPhysicalRegion(originalRegion, bounds, dpr);

    QCOMPARE(normalized, QRect(0, 0, 16, 16));
    QVERIFY(bounds.contains(normalized));
    verifyEvenPhysicalSize(normalized, dpr);
}

void TestRecordingRegionSizing::testFullScreenOddRegionFallsBackToCrop()
{
    const QRect bounds(0, 0, 101, 51);
    const QRect originalRegion = bounds;

    const QRect normalized = normalizeToEvenPhysicalRegion(originalRegion, bounds, 1.0);

    QCOMPARE(normalized, QRect(0, 0, 100, 50));
    QVERIFY(bounds.contains(normalized));
    verifyEvenPhysicalSize(normalized, 1.0);
}

void TestRecordingRegionSizing::testFullScreenOddRegionCropsMoreThanOneStepAtFractionalDpr()
{
    const QRect bounds(0, 0, 4, 2);
    const QRect originalRegion = bounds;
    constexpr qreal dpr = 1.75;

    const QRect normalized = normalizeToEvenPhysicalRegion(originalRegion, bounds, dpr);

    QCOMPARE(normalized, QRect(0, 0, 2, 2));
    QVERIFY(bounds.contains(normalized));
    verifyEvenPhysicalSize(normalized, dpr);
}

void TestRecordingRegionSizing::testHighDpiNormalizationAt15()
{
    const QRect bounds(0, 0, 400, 300);
    const QRect originalRegion(20, 20, 99, 79);
    constexpr qreal dpr = 1.5;

    const QRect normalized = normalizeToEvenPhysicalRegion(originalRegion, bounds, dpr);

    QCOMPARE(normalized, QRect(20, 20, 100, 80));
    QVERIFY(bounds.contains(normalized));
    verifyEvenPhysicalSize(normalized, dpr);
}

void TestRecordingRegionSizing::testHighDpiNormalizationAt11()
{
    const QRect bounds(0, 0, 120, 80);
    const QRect originalRegion(65, 43, 55, 37); // right/bottom already touch bounds
    constexpr qreal dpr = 1.1;

    const QRect normalized = normalizeToEvenPhysicalRegion(originalRegion, bounds, dpr);

    QCOMPARE(normalized, QRect(64, 42, 56, 38));
    QVERIFY(bounds.contains(normalized));
    verifyEvenPhysicalSize(normalized, dpr);
}

QTEST_MAIN(TestRecordingRegionSizing)
#include "tst_RegionSizing.moc"
