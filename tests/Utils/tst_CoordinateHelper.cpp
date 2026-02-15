#include <QtTest>
#include <QApplication>
#include <QScreen>
#include "utils/CoordinateHelper.h"

/**
 * @brief Unit tests for CoordinateHelper utility class.
 *
 * Tests coordinate conversion functions including:
 * - Device pixel ratio handling
 * - Logical to Physical coordinate conversions
 * - Physical to Logical coordinate conversions
 * - Even physical size calculation for video encoders
 */
class tst_CoordinateHelper : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // getDevicePixelRatio tests
    void testGetDevicePixelRatio_NullScreen();
    void testGetDevicePixelRatio_ValidScreen();

    // toPhysical(QPoint) tests
    void testToPhysicalPoint_DPR1();
    void testToPhysicalPoint_DPR2();
    void testToPhysicalPoint_DPR1_5();
    void testToPhysicalPointF_DPR1_25();
    void testToPhysicalPoint_NegativeCoordinates();
    void testToPhysicalPoint_ZeroCoordinates();
    void testToPhysicalPoint_LargeCoordinates();

    // toPhysical(QRect) tests
    void testToPhysicalRect_DPR1();
    void testToPhysicalRect_DPR2();
    void testToPhysicalRect_DPR1_5();
    void testToPhysicalRect_DPR1_25();
    void testToPhysicalCoveringRect_DPR1_5();
    void testToPhysicalCoveringRect_RightEdgeFractionalDpr();
    void testToPhysicalRect_NegativePosition();

    // toPhysical(QSize) tests
    void testToPhysicalSize_DPR1();
    void testToPhysicalSize_DPR2();
    void testToPhysicalSize_DPR1_5();
    void testToPhysicalSize_ZeroSize();

    // toLogical(QPoint) tests
    void testToLogicalPoint_DPR1();
    void testToLogicalPoint_DPR2();
    void testToLogicalPoint_DPR1_5();
    void testToLogicalPoint_DPR0_ReturnsOriginal();
    void testToLogicalPoint_NegativeCoordinates();

    // toLogical(QRect) tests
    void testToLogicalRect_DPR1();
    void testToLogicalRect_DPR2();
    void testToLogicalRect_DPR1_25();
    void testToLogicalRect_DPR0_ReturnsOriginal();

    // toLogical(QSize) tests
    void testToLogicalSize_DPR1();
    void testToLogicalSize_DPR2();
    void testToLogicalSize_DPR0_ReturnsOriginal();

    // Roundtrip tests
    void testPhysicalLogicalRoundtrip_Point_DPR1();
    void testPhysicalLogicalRoundtrip_Point_DPR2();

    // toEvenPhysicalSize tests
    void testToEvenPhysicalSize_EvenInput();
    void testToEvenPhysicalSize_OddWidth();
    void testToEvenPhysicalSize_OddHeight();
    void testToEvenPhysicalSize_BothOdd();
    void testToEvenPhysicalSize_DPR2_EvenResult();
    void testToEvenPhysicalSize_DPR1_5();
    void testToEvenPhysicalSize_SmallSize();
    void testToEvenPhysicalSize_ZeroSize();
};

void tst_CoordinateHelper::initTestCase()
{
}

void tst_CoordinateHelper::cleanupTestCase()
{
}

// ============================================================================
// getDevicePixelRatio tests
// ============================================================================

void tst_CoordinateHelper::testGetDevicePixelRatio_NullScreen()
{
    qreal dpr = CoordinateHelper::getDevicePixelRatio(nullptr);
    QCOMPARE(dpr, 1.0);
}

void tst_CoordinateHelper::testGetDevicePixelRatio_ValidScreen()
{
    QScreen* screen = QApplication::primaryScreen();
    if (!screen) {
        QSKIP("No screen available for testing");
    }

    qreal dpr = CoordinateHelper::getDevicePixelRatio(screen);
    QVERIFY(dpr >= 1.0);  // DPR is always >= 1.0
    QCOMPARE(dpr, screen->devicePixelRatio());
}

// ============================================================================
// toPhysical(QPoint) tests
// ============================================================================

void tst_CoordinateHelper::testToPhysicalPoint_DPR1()
{
    QPoint logical(100, 200);
    QPoint physical = CoordinateHelper::toPhysical(logical, 1.0);
    QCOMPARE(physical, QPoint(100, 200));
}

void tst_CoordinateHelper::testToPhysicalPoint_DPR2()
{
    QPoint logical(100, 200);
    QPoint physical = CoordinateHelper::toPhysical(logical, 2.0);
    QCOMPARE(physical, QPoint(200, 400));
}

void tst_CoordinateHelper::testToPhysicalPoint_DPR1_5()
{
    QPoint logical(100, 200);
    QPoint physical = CoordinateHelper::toPhysical(logical, 1.5);
    // 100 * 1.5 = 150, 200 * 1.5 = 300
    QCOMPARE(physical, QPoint(150, 300));

    // Test rounding: 101 * 1.5 = 151.5 -> rounds to 152
    QPoint logical2(101, 201);
    QPoint physical2 = CoordinateHelper::toPhysical(logical2, 1.5);
    QCOMPARE(physical2, QPoint(152, 302));  // qRound(151.5)=152, qRound(301.5)=302
}

void tst_CoordinateHelper::testToPhysicalPointF_DPR1_25()
{
    QPointF logical(10.2, 20.6);
    QPoint physical = CoordinateHelper::toPhysical(logical, 1.25);
    QCOMPARE(physical, QPoint(13, 26));
}

void tst_CoordinateHelper::testToPhysicalPoint_NegativeCoordinates()
{
    QPoint logical(-100, -200);
    QPoint physical = CoordinateHelper::toPhysical(logical, 2.0);
    QCOMPARE(physical, QPoint(-200, -400));
}

void tst_CoordinateHelper::testToPhysicalPoint_ZeroCoordinates()
{
    QPoint logical(0, 0);
    QPoint physical = CoordinateHelper::toPhysical(logical, 2.0);
    QCOMPARE(physical, QPoint(0, 0));
}

void tst_CoordinateHelper::testToPhysicalPoint_LargeCoordinates()
{
    QPoint logical(10000, 20000);
    QPoint physical = CoordinateHelper::toPhysical(logical, 2.0);
    QCOMPARE(physical, QPoint(20000, 40000));
}

// ============================================================================
// toPhysical(QRect) tests
// ============================================================================

void tst_CoordinateHelper::testToPhysicalRect_DPR1()
{
    QRect logical(10, 20, 100, 200);
    QRect physical = CoordinateHelper::toPhysical(logical, 1.0);
    QCOMPARE(physical, QRect(10, 20, 100, 200));
}

void tst_CoordinateHelper::testToPhysicalRect_DPR2()
{
    QRect logical(10, 20, 100, 200);
    QRect physical = CoordinateHelper::toPhysical(logical, 2.0);
    QCOMPARE(physical, QRect(20, 40, 200, 400));
}

void tst_CoordinateHelper::testToPhysicalRect_DPR1_5()
{
    QRect logical(10, 20, 100, 200);
    QRect physical = CoordinateHelper::toPhysical(logical, 1.5);
    QCOMPARE(physical, QRect(15, 30, 150, 300));
}

void tst_CoordinateHelper::testToPhysicalRect_DPR1_25()
{
    QRect logical(3, 5, 7, 9);
    QRect physical = CoordinateHelper::toPhysical(logical, 1.25);
    QCOMPARE(physical, QRect(4, 6, 9, 11));
}

void tst_CoordinateHelper::testToPhysicalCoveringRect_DPR1_5()
{
    QRect logical(1, 1, 3, 3);
    QRect covering = CoordinateHelper::toPhysicalCoveringRect(logical, 1.5);
    QCOMPARE(covering, QRect(1, 1, 5, 5));
}

void tst_CoordinateHelper::testToPhysicalCoveringRect_RightEdgeFractionalDpr()
{
    const qreal dpr = 1.5;
    const QSize logicalScreenSize(1920, 1080);
    const QSize physicalScreenSize = CoordinateHelper::toPhysical(logicalScreenSize, dpr);
    const QRect physicalScreenRect(QPoint(0, 0), physicalScreenSize);

    // Right-most 1 logical pixel should remain inside the physical screen bounds.
    const QRect logicalRegion(1919, 100, 1, 1);
    const QRect covering = CoordinateHelper::toPhysicalCoveringRect(logicalRegion, dpr);

    QCOMPARE(covering, QRect(2878, 150, 2, 2));
    QVERIFY(physicalScreenRect.contains(covering));
}

void tst_CoordinateHelper::testToPhysicalRect_NegativePosition()
{
    QRect logical(-50, -100, 100, 200);
    QRect physical = CoordinateHelper::toPhysical(logical, 2.0);
    QCOMPARE(physical, QRect(-100, -200, 200, 400));
}

// ============================================================================
// toPhysical(QSize) tests
// ============================================================================

void tst_CoordinateHelper::testToPhysicalSize_DPR1()
{
    QSize logical(100, 200);
    QSize physical = CoordinateHelper::toPhysical(logical, 1.0);
    QCOMPARE(physical, QSize(100, 200));
}

void tst_CoordinateHelper::testToPhysicalSize_DPR2()
{
    QSize logical(100, 200);
    QSize physical = CoordinateHelper::toPhysical(logical, 2.0);
    QCOMPARE(physical, QSize(200, 400));
}

void tst_CoordinateHelper::testToPhysicalSize_DPR1_5()
{
    QSize logical(100, 200);
    QSize physical = CoordinateHelper::toPhysical(logical, 1.5);
    QCOMPARE(physical, QSize(150, 300));
}

void tst_CoordinateHelper::testToPhysicalSize_ZeroSize()
{
    QSize logical(0, 0);
    QSize physical = CoordinateHelper::toPhysical(logical, 2.0);
    QCOMPARE(physical, QSize(0, 0));
}

// ============================================================================
// toLogical(QPoint) tests
// ============================================================================

void tst_CoordinateHelper::testToLogicalPoint_DPR1()
{
    QPoint physical(100, 200);
    QPoint logical = CoordinateHelper::toLogical(physical, 1.0);
    QCOMPARE(logical, QPoint(100, 200));
}

void tst_CoordinateHelper::testToLogicalPoint_DPR2()
{
    QPoint physical(200, 400);
    QPoint logical = CoordinateHelper::toLogical(physical, 2.0);
    QCOMPARE(logical, QPoint(100, 200));
}

void tst_CoordinateHelper::testToLogicalPoint_DPR1_5()
{
    QPoint physical(150, 300);
    QPoint logical = CoordinateHelper::toLogical(physical, 1.5);
    QCOMPARE(logical, QPoint(100, 200));

    // Test rounding: 151 / 1.5 = 100.67 -> rounds to 101
    QPoint physical2(151, 301);
    QPoint logical2 = CoordinateHelper::toLogical(physical2, 1.5);
    QCOMPARE(logical2, QPoint(101, 201));  // qRound(100.67)=101, qRound(200.67)=201
}

void tst_CoordinateHelper::testToLogicalPoint_DPR0_ReturnsOriginal()
{
    QPoint physical(100, 200);
    QPoint logical = CoordinateHelper::toLogical(physical, 0.0);
    QCOMPARE(logical, QPoint(100, 200));  // qFuzzyIsNull check
}

void tst_CoordinateHelper::testToLogicalPoint_NegativeCoordinates()
{
    QPoint physical(-200, -400);
    QPoint logical = CoordinateHelper::toLogical(physical, 2.0);
    QCOMPARE(logical, QPoint(-100, -200));
}

// ============================================================================
// toLogical(QRect) tests
// ============================================================================

void tst_CoordinateHelper::testToLogicalRect_DPR1()
{
    QRect physical(10, 20, 100, 200);
    QRect logical = CoordinateHelper::toLogical(physical, 1.0);
    QCOMPARE(logical, QRect(10, 20, 100, 200));
}

void tst_CoordinateHelper::testToLogicalRect_DPR2()
{
    QRect physical(20, 40, 200, 400);
    QRect logical = CoordinateHelper::toLogical(physical, 2.0);
    QCOMPARE(logical, QRect(10, 20, 100, 200));
}

void tst_CoordinateHelper::testToLogicalRect_DPR1_25()
{
    QRect physical(4, 6, 9, 11);
    QRect logical = CoordinateHelper::toLogical(physical, 1.25);
    QCOMPARE(logical, QRect(3, 5, 7, 9));
}

void tst_CoordinateHelper::testToLogicalRect_DPR0_ReturnsOriginal()
{
    QRect physical(10, 20, 100, 200);
    QRect logical = CoordinateHelper::toLogical(physical, 0.0);
    QCOMPARE(logical, QRect(10, 20, 100, 200));
}

// ============================================================================
// toLogical(QSize) tests
// ============================================================================

void tst_CoordinateHelper::testToLogicalSize_DPR1()
{
    QSize physical(100, 200);
    QSize logical = CoordinateHelper::toLogical(physical, 1.0);
    QCOMPARE(logical, QSize(100, 200));
}

void tst_CoordinateHelper::testToLogicalSize_DPR2()
{
    QSize physical(200, 400);
    QSize logical = CoordinateHelper::toLogical(physical, 2.0);
    QCOMPARE(logical, QSize(100, 200));
}

void tst_CoordinateHelper::testToLogicalSize_DPR0_ReturnsOriginal()
{
    QSize physical(100, 200);
    QSize logical = CoordinateHelper::toLogical(physical, 0.0);
    QCOMPARE(logical, QSize(100, 200));
}

// ============================================================================
// Roundtrip tests
// ============================================================================

void tst_CoordinateHelper::testPhysicalLogicalRoundtrip_Point_DPR1()
{
    QPoint original(100, 200);
    QPoint physical = CoordinateHelper::toPhysical(original, 1.0);
    QPoint logical = CoordinateHelper::toLogical(physical, 1.0);
    QCOMPARE(logical, original);
}

void tst_CoordinateHelper::testPhysicalLogicalRoundtrip_Point_DPR2()
{
    QPoint original(100, 200);
    QPoint physical = CoordinateHelper::toPhysical(original, 2.0);
    QPoint logical = CoordinateHelper::toLogical(physical, 2.0);
    QCOMPARE(logical, original);
}

// ============================================================================
// toEvenPhysicalSize tests
// ============================================================================

void tst_CoordinateHelper::testToEvenPhysicalSize_EvenInput()
{
    QSize logical(100, 200);
    QSize result = CoordinateHelper::toEvenPhysicalSize(logical, 1.0);
    QCOMPARE(result, QSize(100, 200));
    QCOMPARE(result.width() % 2, 0);
    QCOMPARE(result.height() % 2, 0);
}

void tst_CoordinateHelper::testToEvenPhysicalSize_OddWidth()
{
    QSize logical(101, 200);
    QSize result = CoordinateHelper::toEvenPhysicalSize(logical, 1.0);
    QCOMPARE(result, QSize(102, 200));  // 101 -> 102
    QCOMPARE(result.width() % 2, 0);
    QCOMPARE(result.height() % 2, 0);
}

void tst_CoordinateHelper::testToEvenPhysicalSize_OddHeight()
{
    QSize logical(100, 201);
    QSize result = CoordinateHelper::toEvenPhysicalSize(logical, 1.0);
    QCOMPARE(result, QSize(100, 202));  // 201 -> 202
    QCOMPARE(result.width() % 2, 0);
    QCOMPARE(result.height() % 2, 0);
}

void tst_CoordinateHelper::testToEvenPhysicalSize_BothOdd()
{
    QSize logical(101, 201);
    QSize result = CoordinateHelper::toEvenPhysicalSize(logical, 1.0);
    QCOMPARE(result, QSize(102, 202));
    QCOMPARE(result.width() % 2, 0);
    QCOMPARE(result.height() % 2, 0);
}

void tst_CoordinateHelper::testToEvenPhysicalSize_DPR2_EvenResult()
{
    // 50 * 2 = 100 (even), 75 * 2 = 150 (even)
    QSize logical(50, 75);
    QSize result = CoordinateHelper::toEvenPhysicalSize(logical, 2.0);
    QCOMPARE(result, QSize(100, 150));
    QCOMPARE(result.width() % 2, 0);
    QCOMPARE(result.height() % 2, 0);
}

void tst_CoordinateHelper::testToEvenPhysicalSize_DPR1_5()
{
    // 101 * 1.5 = 151.5 -> rounds to 152 (even)
    // 101 * 1.5 = 151.5 -> rounds to 152 (even)
    QSize logical(101, 101);
    QSize result = CoordinateHelper::toEvenPhysicalSize(logical, 1.5);
    QCOMPARE(result.width() % 2, 0);
    QCOMPARE(result.height() % 2, 0);
}

void tst_CoordinateHelper::testToEvenPhysicalSize_SmallSize()
{
    QSize logical(1, 1);
    QSize result = CoordinateHelper::toEvenPhysicalSize(logical, 1.0);
    QCOMPARE(result, QSize(2, 2));  // 1 -> 2
    QCOMPARE(result.width() % 2, 0);
    QCOMPARE(result.height() % 2, 0);
}

void tst_CoordinateHelper::testToEvenPhysicalSize_ZeroSize()
{
    QSize logical(0, 0);
    QSize result = CoordinateHelper::toEvenPhysicalSize(logical, 2.0);
    QCOMPARE(result, QSize(0, 0));
    QCOMPARE(result.width() % 2, 0);
    QCOMPARE(result.height() % 2, 0);
}

QTEST_MAIN(tst_CoordinateHelper)
#include "tst_CoordinateHelper.moc"
