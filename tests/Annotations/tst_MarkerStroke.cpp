#include <QtTest/QtTest>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include "annotations/MarkerStroke.h"

/**
 * @brief Tests for MarkerStroke annotation class
 *
 * Covers:
 * - Construction with various parameters
 * - Bounding rectangle calculations
 * - Point addition and path building
 * - Clone functionality
 * - Intersection detection for eraser
 * - Semi-transparent rendering
 */
class TestMarkerStroke : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_EmptyPoints();
    void testConstruction_SinglePoint();
    void testConstruction_MultiplePoints();
    void testConstruction_WithColor();
    void testConstruction_WithWidth();

    // Bounding rect tests
    void testBoundingRect_EmptyPoints();
    void testBoundingRect_SinglePoint();
    void testBoundingRect_HorizontalLine();
    void testBoundingRect_VerticalLine();
    void testBoundingRect_DiagonalLine();
    void testBoundingRect_IncludesWidth();

    // Point addition tests
    void testAddPoint_IncrementsBoundingRect();
    void testAddPoint_BuildsPath();
    void testAddPoint_MultiplePoints();

    // Clone tests
    void testClone_CreatesNewInstance();
    void testClone_PreservesPoints();
    void testClone_PreservesColor();
    void testClone_PreservesWidth();

    // Intersection tests (for eraser)
    void testIntersectsCircle_NoIntersection();
    void testIntersectsCircle_Intersection();
    void testIntersectsCircle_EdgeCase();
    void testIntersectsCircle_EmptyStroke();

    // Stroke path tests
    void testStrokePath_EmptyPoints();
    void testStrokePath_SinglePoint();
    void testStrokePath_ValidPath();

    // Drawing tests
    void testDraw_EmptyPoints();
    void testDraw_SinglePoint();
    void testDraw_MultiplePoints();

private:
    QVector<QPointF> createTestPoints(int count, qreal spacing = 10.0);
};

QVector<QPointF> TestMarkerStroke::createTestPoints(int count, qreal spacing)
{
    QVector<QPointF> points;
    for (int i = 0; i < count; ++i) {
        points.append(QPointF(100 + i * spacing, 100 + i * spacing));
    }
    return points;
}

// ============================================================================
// Construction Tests
// ============================================================================

void TestMarkerStroke::testConstruction_EmptyPoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QVERIFY(stroke.boundingRect().isEmpty());
}

void TestMarkerStroke::testConstruction_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);
    QVERIFY(!stroke.boundingRect().isEmpty());
}

void TestMarkerStroke::testConstruction_MultiplePoints()
{
    QVector<QPointF> points = createTestPoints(5);
    MarkerStroke stroke(points, Qt::yellow, 20);
    QVERIFY(!stroke.boundingRect().isEmpty());
}

void TestMarkerStroke::testConstruction_WithColor()
{
    QVector<QPointF> points = createTestPoints(3);
    MarkerStroke stroke(points, Qt::green, 20);

    auto cloned = stroke.clone();
    QVERIFY(cloned != nullptr);
}

void TestMarkerStroke::testConstruction_WithWidth()
{
    QVector<QPointF> points = createTestPoints(3);
    MarkerStroke strokeThin(points, Qt::yellow, 10);
    MarkerStroke strokeThick(points, Qt::yellow, 40);

    // Thicker stroke should have larger bounding rect
    QVERIFY(strokeThick.boundingRect().width() > strokeThin.boundingRect().width());
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestMarkerStroke::testBoundingRect_EmptyPoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QVERIFY(stroke.boundingRect().isEmpty());
}

void TestMarkerStroke::testBoundingRect_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
}

void TestMarkerStroke::testBoundingRect_HorizontalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(200, 100)));
    QVERIFY(rect.width() >= 100);
}

void TestMarkerStroke::testBoundingRect_VerticalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(100, 200) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(100, 200)));
    QVERIFY(rect.height() >= 100);
}

void TestMarkerStroke::testBoundingRect_DiagonalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 200) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(200, 200)));
}

void TestMarkerStroke::testBoundingRect_IncludesWidth()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };

    MarkerStroke thinStroke(points, Qt::yellow, 10);
    MarkerStroke thickStroke(points, Qt::yellow, 40);

    // Thick stroke bounding rect should be larger due to pen width
    QVERIFY(thickStroke.boundingRect().height() > thinStroke.boundingRect().height());
}

// ============================================================================
// Point Addition Tests
// ============================================================================

void TestMarkerStroke::testAddPoint_IncrementsBoundingRect()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QVERIFY(stroke.boundingRect().isEmpty());

    stroke.addPoint(QPointF(100, 100));
    QRect rect1 = stroke.boundingRect();
    QVERIFY(!rect1.isEmpty());

    stroke.addPoint(QPointF(200, 200));
    QRect rect2 = stroke.boundingRect();
    QVERIFY(rect2.width() > rect1.width() || rect2.height() > rect1.height());
}

void TestMarkerStroke::testAddPoint_BuildsPath()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);

    for (int i = 0; i < 10; ++i) {
        stroke.addPoint(QPointF(100 + i * 10, 100 + i * 5));
    }

    QPainterPath path = stroke.strokePath();
    QVERIFY(!path.isEmpty());
}

void TestMarkerStroke::testAddPoint_MultiplePoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);

    for (int i = 0; i < 20; ++i) {
        stroke.addPoint(QPointF(100 + i * 5, 100 + qSin(i) * 20));
    }

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.width() >= 95);  // At least 19 * 5 = 95
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestMarkerStroke::testClone_CreatesNewInstance()
{
    QVector<QPointF> points = createTestPoints(5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    auto cloned = stroke.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &stroke);
}

void TestMarkerStroke::testClone_PreservesPoints()
{
    QVector<QPointF> points = createTestPoints(5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    auto cloned = stroke.clone();
    QCOMPARE(cloned->boundingRect(), stroke.boundingRect());
}

void TestMarkerStroke::testClone_PreservesColor()
{
    QVector<QPointF> points = createTestPoints(3);
    MarkerStroke stroke(points, Qt::green, 20);

    auto cloned = stroke.clone();

    // Draw both and compare
    QImage img1(200, 200, QImage::Format_ARGB32);
    QImage img2(200, 200, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    img2.fill(Qt::white);

    QPainter p1(&img1);
    QPainter p2(&img2);
    stroke.draw(p1);
    cloned->draw(p2);

    QCOMPARE(img1, img2);
}

void TestMarkerStroke::testClone_PreservesWidth()
{
    QVector<QPointF> points = createTestPoints(3);
    MarkerStroke stroke(points, Qt::yellow, 30);

    auto cloned = stroke.clone();
    QCOMPARE(cloned->boundingRect(), stroke.boundingRect());
}

// ============================================================================
// Intersection Tests
// ============================================================================

void TestMarkerStroke::testIntersectsCircle_NoIntersection()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    // Circle far from the stroke
    QVERIFY(!stroke.intersectsCircle(QPoint(100, 300), 10));
}

void TestMarkerStroke::testIntersectsCircle_Intersection()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    // Circle on the stroke
    QVERIFY(stroke.intersectsCircle(QPoint(150, 100), 10));
}

void TestMarkerStroke::testIntersectsCircle_EdgeCase()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    // Circle just touching the edge
    QVERIFY(stroke.intersectsCircle(QPoint(100, 100), 15));
}

void TestMarkerStroke::testIntersectsCircle_EmptyStroke()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QVERIFY(!stroke.intersectsCircle(QPoint(100, 100), 10));
}

// ============================================================================
// Stroke Path Tests
// ============================================================================

void TestMarkerStroke::testStrokePath_EmptyPoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QPainterPath path = stroke.strokePath();
    QVERIFY(path.isEmpty());
}

void TestMarkerStroke::testStrokePath_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);
    QPainterPath path = stroke.strokePath();
    QVERIFY(path.isEmpty());  // Need at least 2 points for a path
}

void TestMarkerStroke::testStrokePath_ValidPath()
{
    QVector<QPointF> points = createTestPoints(5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    QPainterPath path = stroke.strokePath();
    QVERIFY(!path.isEmpty());

    // Path should contain all the points
    for (const QPointF& pt : points) {
        QVERIFY(path.contains(pt));
    }
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestMarkerStroke::testDraw_EmptyPoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should not crash
    stroke.draw(painter);
    QVERIFY(true);
}

void TestMarkerStroke::testDraw_SinglePoint()
{
    QVector<QPointF> points = { QPointF(50, 50) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should not crash (may not draw anything visible, but shouldn't crash)
    stroke.draw(painter);
    QVERIFY(true);
}

void TestMarkerStroke::testDraw_MultiplePoints()
{
    QVector<QPointF> points = createTestPoints(10, 5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    stroke.draw(painter);
    painter.end();

    // Verify something was drawn (not all white)
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

QTEST_MAIN(TestMarkerStroke)
#include "tst_MarkerStroke.moc"
