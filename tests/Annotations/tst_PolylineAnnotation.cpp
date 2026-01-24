#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "annotations/PolylineAnnotation.h"

/**
 * @brief Tests for PolylineAnnotation class
 *
 * Covers:
 * - Construction with various parameters
 * - Point management (add, update, remove, set)
 * - Bounding rectangle calculations
 * - Clone functionality
 * - Hit testing (containsPoint)
 * - Line end styles
 * - Drawing tests
 */
class TestPolylineAnnotation : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_Empty();
    void testConstruction_WithPoints();
    void testConstruction_WithColor();
    void testConstruction_WithWidth();
    void testConstruction_WithLineEndStyle();
    void testConstruction_WithLineStyle();

    // Point management tests
    void testAddPoint();
    void testUpdateLastPoint();
    void testSetPoint();
    void testRemoveLastPoint();
    void testMoveBy();
    void testPointCount();
    void testPoints();

    // Bounding rect tests
    void testBoundingRect_Empty();
    void testBoundingRect_SinglePoint();
    void testBoundingRect_MultiplePoints();
    void testBoundingRect_AfterAddPoint();

    // Clone tests
    void testClone_CreatesNewInstance();
    void testClone_PreservesPoints();
    void testClone_PreservesColor();
    void testClone_PreservesStyle();

    // Hit testing tests
    void testContainsPoint_OnSegment();
    void testContainsPoint_OffSegment();
    void testContainsPoint_NearVertex();
    void testContainsPoint_MultipleSegments();

    // Style accessors
    void testLineEndStyle();
    void testColor();
    void testWidth();

    // Drawing tests
    void testDraw_Empty();
    void testDraw_SinglePoint();
    void testDraw_MultiplePoints();
    void testDraw_AllLineEndStyles();
    void testDraw_AllLineStyles();

private:
    QVector<QPoint> createTestPoints(int count, int spacing = 20);
};

QVector<QPoint> TestPolylineAnnotation::createTestPoints(int count, int spacing)
{
    QVector<QPoint> points;
    for (int i = 0; i < count; ++i) {
        points.append(QPoint(100 + i * spacing, 100));
    }
    return points;
}

// ============================================================================
// Construction Tests
// ============================================================================

void TestPolylineAnnotation::testConstruction_Empty()
{
    PolylineAnnotation polyline(Qt::red, 3);

    QCOMPARE(polyline.pointCount(), 0);
    QVERIFY(polyline.boundingRect().isEmpty());
}

void TestPolylineAnnotation::testConstruction_WithPoints()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QCOMPARE(polyline.pointCount(), 4);
    QVERIFY(!polyline.boundingRect().isEmpty());
}

void TestPolylineAnnotation::testConstruction_WithColor()
{
    PolylineAnnotation polyline(Qt::blue, 3);
    QCOMPARE(polyline.color(), QColor(Qt::blue));
}

void TestPolylineAnnotation::testConstruction_WithWidth()
{
    PolylineAnnotation polyline(Qt::red, 10);
    QCOMPARE(polyline.width(), 10);
}

void TestPolylineAnnotation::testConstruction_WithLineEndStyle()
{
    PolylineAnnotation polylineNone(Qt::red, 3, LineEndStyle::None);
    PolylineAnnotation polylineEnd(Qt::red, 3, LineEndStyle::EndArrow);
    PolylineAnnotation polylineBoth(Qt::red, 3, LineEndStyle::BothArrow);

    QCOMPARE(polylineNone.lineEndStyle(), LineEndStyle::None);
    QCOMPARE(polylineEnd.lineEndStyle(), LineEndStyle::EndArrow);
    QCOMPARE(polylineBoth.lineEndStyle(), LineEndStyle::BothArrow);
}

void TestPolylineAnnotation::testConstruction_WithLineStyle()
{
    QVector<QPoint> points = createTestPoints(3);

    PolylineAnnotation solidPolyline(points, Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Solid);
    PolylineAnnotation dashedPolyline(points, Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Dashed);
    PolylineAnnotation dottedPolyline(points, Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Dotted);

    // All should have valid bounding rects
    QVERIFY(!solidPolyline.boundingRect().isEmpty());
    QVERIFY(!dashedPolyline.boundingRect().isEmpty());
    QVERIFY(!dottedPolyline.boundingRect().isEmpty());
}

// ============================================================================
// Point Management Tests
// ============================================================================

void TestPolylineAnnotation::testAddPoint()
{
    PolylineAnnotation polyline(Qt::red, 3);

    polyline.addPoint(QPoint(100, 100));
    QCOMPARE(polyline.pointCount(), 1);

    polyline.addPoint(QPoint(200, 100));
    QCOMPARE(polyline.pointCount(), 2);
}

void TestPolylineAnnotation::testUpdateLastPoint()
{
    PolylineAnnotation polyline(Qt::red, 3);
    polyline.addPoint(QPoint(100, 100));
    polyline.addPoint(QPoint(200, 100));

    polyline.updateLastPoint(QPoint(250, 150));

    QVector<QPoint> points = polyline.points();
    QCOMPARE(points.last(), QPoint(250, 150));
}

void TestPolylineAnnotation::testSetPoint()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    polyline.setPoint(1, QPoint(150, 200));

    QVector<QPoint> resultPoints = polyline.points();
    QCOMPARE(resultPoints[1], QPoint(150, 200));
}

void TestPolylineAnnotation::testRemoveLastPoint()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QCOMPARE(polyline.pointCount(), 4);

    polyline.removeLastPoint();
    QCOMPARE(polyline.pointCount(), 3);
}

void TestPolylineAnnotation::testMoveBy()
{
    QVector<QPoint> points = createTestPoints(3);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QVector<QPoint> originalPoints = polyline.points();
    polyline.moveBy(QPoint(50, 50));

    QVector<QPoint> movedPoints = polyline.points();
    for (int i = 0; i < originalPoints.size(); ++i) {
        QCOMPARE(movedPoints[i], originalPoints[i] + QPoint(50, 50));
    }
}

void TestPolylineAnnotation::testPointCount()
{
    PolylineAnnotation polyline(Qt::red, 3);
    QCOMPARE(polyline.pointCount(), 0);

    polyline.addPoint(QPoint(100, 100));
    QCOMPARE(polyline.pointCount(), 1);

    polyline.addPoint(QPoint(200, 100));
    polyline.addPoint(QPoint(300, 100));
    QCOMPARE(polyline.pointCount(), 3);
}

void TestPolylineAnnotation::testPoints()
{
    QVector<QPoint> points = createTestPoints(3);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QVector<QPoint> resultPoints = polyline.points();
    QCOMPARE(resultPoints.size(), points.size());
    for (int i = 0; i < points.size(); ++i) {
        QCOMPARE(resultPoints[i], points[i]);
    }
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestPolylineAnnotation::testBoundingRect_Empty()
{
    PolylineAnnotation polyline(Qt::red, 3);
    QVERIFY(polyline.boundingRect().isEmpty());
}

void TestPolylineAnnotation::testBoundingRect_SinglePoint()
{
    PolylineAnnotation polyline(Qt::red, 3);
    polyline.addPoint(QPoint(100, 100));

    QRect rect = polyline.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
}

void TestPolylineAnnotation::testBoundingRect_MultiplePoints()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 50), QPoint(300, 150) };
    PolylineAnnotation polyline(points, Qt::red, 3);

    QRect rect = polyline.boundingRect();
    for (const QPoint& pt : points) {
        QVERIFY(rect.contains(pt));
    }
}

void TestPolylineAnnotation::testBoundingRect_AfterAddPoint()
{
    PolylineAnnotation polyline(Qt::red, 3);
    polyline.addPoint(QPoint(100, 100));

    QRect rect1 = polyline.boundingRect();

    polyline.addPoint(QPoint(300, 300));
    QRect rect2 = polyline.boundingRect();

    QVERIFY(rect2.width() > rect1.width() || rect2.height() > rect1.height());
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestPolylineAnnotation::testClone_CreatesNewInstance()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    auto cloned = polyline.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &polyline);
}

void TestPolylineAnnotation::testClone_PreservesPoints()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    auto cloned = polyline.clone();
    auto* clonedPolyline = dynamic_cast<PolylineAnnotation*>(cloned.get());

    QVERIFY(clonedPolyline != nullptr);
    QCOMPARE(clonedPolyline->pointCount(), polyline.pointCount());
    QCOMPARE(clonedPolyline->points(), polyline.points());
}

void TestPolylineAnnotation::testClone_PreservesColor()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::blue, 3);

    auto cloned = polyline.clone();
    auto* clonedPolyline = dynamic_cast<PolylineAnnotation*>(cloned.get());

    QVERIFY(clonedPolyline != nullptr);
    QCOMPARE(clonedPolyline->color(), polyline.color());
}

void TestPolylineAnnotation::testClone_PreservesStyle()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 5, LineEndStyle::BothArrow, LineStyle::Dashed);

    auto cloned = polyline.clone();
    auto* clonedPolyline = dynamic_cast<PolylineAnnotation*>(cloned.get());

    QVERIFY(clonedPolyline != nullptr);
    QCOMPARE(clonedPolyline->lineEndStyle(), LineEndStyle::BothArrow);
    QCOMPARE(clonedPolyline->width(), 5);
}

// ============================================================================
// Hit Testing Tests
// ============================================================================

void TestPolylineAnnotation::testContainsPoint_OnSegment()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 100) };
    PolylineAnnotation polyline(points, Qt::red, 5);

    // Point on the middle of the segment
    QVERIFY(polyline.containsPoint(QPoint(150, 100)));
}

void TestPolylineAnnotation::testContainsPoint_OffSegment()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 100) };
    PolylineAnnotation polyline(points, Qt::red, 5);

    // Point far from the segment
    QVERIFY(!polyline.containsPoint(QPoint(150, 300)));
}

void TestPolylineAnnotation::testContainsPoint_NearVertex()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 100), QPoint(200, 200) };
    PolylineAnnotation polyline(points, Qt::red, 5);

    // Point near a vertex
    QVERIFY(polyline.containsPoint(QPoint(200, 100)));
}

void TestPolylineAnnotation::testContainsPoint_MultipleSegments()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 100), QPoint(200, 200), QPoint(100, 200) };
    PolylineAnnotation polyline(points, Qt::red, 5);

    // Points on different segments
    QVERIFY(polyline.containsPoint(QPoint(150, 100)));  // First segment
    QVERIFY(polyline.containsPoint(QPoint(200, 150)));  // Second segment
    QVERIFY(polyline.containsPoint(QPoint(150, 200)));  // Third segment
}

// ============================================================================
// Style Accessors Tests
// ============================================================================

void TestPolylineAnnotation::testLineEndStyle()
{
    PolylineAnnotation polyline(Qt::red, 3, LineEndStyle::EndArrow);
    QCOMPARE(polyline.lineEndStyle(), LineEndStyle::EndArrow);

    polyline.setLineEndStyle(LineEndStyle::BothArrow);
    QCOMPARE(polyline.lineEndStyle(), LineEndStyle::BothArrow);
}

void TestPolylineAnnotation::testColor()
{
    PolylineAnnotation polyline(Qt::green, 3);
    QCOMPARE(polyline.color(), QColor(Qt::green));
}

void TestPolylineAnnotation::testWidth()
{
    PolylineAnnotation polyline(Qt::red, 8);
    QCOMPARE(polyline.width(), 8);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestPolylineAnnotation::testDraw_Empty()
{
    PolylineAnnotation polyline(Qt::red, 3);

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should not crash
    polyline.draw(painter);
    QVERIFY(true);
}

void TestPolylineAnnotation::testDraw_SinglePoint()
{
    PolylineAnnotation polyline(Qt::red, 3);
    polyline.addPoint(QPoint(100, 100));

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should not crash
    polyline.draw(painter);
    QVERIFY(true);
}

void TestPolylineAnnotation::testDraw_MultiplePoints()
{
    QVector<QPoint> points = createTestPoints(5, 30);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QImage image(300, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    polyline.draw(painter);
    painter.end();

    // Verify something was drawn
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

void TestPolylineAnnotation::testDraw_AllLineEndStyles()
{
    QVector<QPoint> points = createTestPoints(3, 40);

    QList<LineEndStyle> styles = {
        LineEndStyle::None,
        LineEndStyle::EndArrow,
        LineEndStyle::EndArrowOutline,
        LineEndStyle::EndArrowLine,
        LineEndStyle::BothArrow,
        LineEndStyle::BothArrowOutline
    };

    for (LineEndStyle style : styles) {
        PolylineAnnotation polyline(points, Qt::red, 3, style);

        QImage image(300, 200, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);

        polyline.draw(painter);
        QVERIFY(true);  // No crash
    }
}

void TestPolylineAnnotation::testDraw_AllLineStyles()
{
    QVector<QPoint> points = createTestPoints(4, 30);

    QList<LineStyle> styles = { LineStyle::Solid, LineStyle::Dashed, LineStyle::Dotted };

    for (LineStyle style : styles) {
        PolylineAnnotation polyline(points, Qt::red, 3, LineEndStyle::EndArrow, style);

        QImage image(300, 200, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);

        polyline.draw(painter);
        QVERIFY(true);  // No crash
    }
}

QTEST_MAIN(TestPolylineAnnotation)
#include "tst_PolylineAnnotation.moc"
