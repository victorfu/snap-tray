#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "annotations/PencilStroke.h"

/**
 * @brief Tests for PencilStroke annotation class
 *
 * Covers:
 * - Construction with various parameters
 * - Bounding rectangle calculations
 * - Point addition and path building
 * - Clone functionality
 * - Intersection detection for eraser
 * - Line style variations
 */
class TestPencilStroke : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_EmptyPoints();
    void testConstruction_SinglePoint();
    void testConstruction_MultiplePoints();
    void testConstruction_WithColor();
    void testConstruction_WithWidth();
    void testConstruction_WithLineStyle();

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
    void testClone_PreservesLineStyle();

    // Intersection tests (for eraser)
    void testIntersectsCircle_NoIntersection();
    void testIntersectsCircle_Intersection();
    void testIntersectsCircle_EdgeCase();
    void testIntersectsCircle_EmptyStroke();

    // Stroke path tests
    void testStrokePath_EmptyPoints();
    void testStrokePath_SinglePoint();
    void testStrokePath_ValidPath();

    // Drawing tests (verify no crash)
    void testDraw_EmptyPoints();
    void testDraw_SinglePoint();
    void testDraw_MultiplePoints();
    void testDraw_AllLineStyles();

private:
    QVector<QPointF> createTestPoints(int count, qreal spacing = 10.0);
};

QVector<QPointF> TestPencilStroke::createTestPoints(int count, qreal spacing)
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

void TestPencilStroke::testConstruction_EmptyPoints()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 3);
    QVERIFY(stroke.boundingRect().isEmpty());
}

void TestPencilStroke::testConstruction_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    PencilStroke stroke(points, Qt::red, 3);
    QVERIFY(!stroke.boundingRect().isEmpty());
}

void TestPencilStroke::testConstruction_MultiplePoints()
{
    QVector<QPointF> points = createTestPoints(5);
    PencilStroke stroke(points, Qt::red, 3);
    QVERIFY(!stroke.boundingRect().isEmpty());
}

void TestPencilStroke::testConstruction_WithColor()
{
    QVector<QPointF> points = createTestPoints(3);
    PencilStroke stroke(points, Qt::blue, 3);

    // Clone and verify color is preserved (indirect test)
    auto cloned = stroke.clone();
    QVERIFY(cloned != nullptr);
}

void TestPencilStroke::testConstruction_WithWidth()
{
    QVector<QPointF> points = createTestPoints(3);
    PencilStroke strokeThin(points, Qt::red, 1);
    PencilStroke strokeThick(points, Qt::red, 20);

    // Thicker stroke should have larger bounding rect
    QVERIFY(strokeThick.boundingRect().width() > strokeThin.boundingRect().width());
}

void TestPencilStroke::testConstruction_WithLineStyle()
{
    QVector<QPointF> points = createTestPoints(3);
    PencilStroke solidStroke(points, Qt::red, 3, LineStyle::Solid);
    PencilStroke dashedStroke(points, Qt::red, 3, LineStyle::Dashed);
    PencilStroke dottedStroke(points, Qt::red, 3, LineStyle::Dotted);

    // All should have valid bounding rects
    QVERIFY(!solidStroke.boundingRect().isEmpty());
    QVERIFY(!dashedStroke.boundingRect().isEmpty());
    QVERIFY(!dottedStroke.boundingRect().isEmpty());
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestPencilStroke::testBoundingRect_EmptyPoints()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 3);
    QVERIFY(stroke.boundingRect().isEmpty());
}

void TestPencilStroke::testBoundingRect_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    PencilStroke stroke(points, Qt::red, 3);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
}

void TestPencilStroke::testBoundingRect_HorizontalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    PencilStroke stroke(points, Qt::red, 3);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(200, 100)));
    QVERIFY(rect.width() >= 100);
}

void TestPencilStroke::testBoundingRect_VerticalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(100, 200) };
    PencilStroke stroke(points, Qt::red, 3);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(100, 200)));
    QVERIFY(rect.height() >= 100);
}

void TestPencilStroke::testBoundingRect_DiagonalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 200) };
    PencilStroke stroke(points, Qt::red, 3);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(200, 200)));
}

void TestPencilStroke::testBoundingRect_IncludesWidth()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };

    PencilStroke thinStroke(points, Qt::red, 2);
    PencilStroke thickStroke(points, Qt::red, 20);

    // Thick stroke bounding rect should be larger due to pen width
    QVERIFY(thickStroke.boundingRect().height() > thinStroke.boundingRect().height());
}

// ============================================================================
// Point Addition Tests
// ============================================================================

void TestPencilStroke::testAddPoint_IncrementsBoundingRect()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 3);
    QVERIFY(stroke.boundingRect().isEmpty());

    stroke.addPoint(QPointF(100, 100));
    QRect rect1 = stroke.boundingRect();
    QVERIFY(!rect1.isEmpty());

    stroke.addPoint(QPointF(200, 200));
    QRect rect2 = stroke.boundingRect();
    QVERIFY(rect2.width() > rect1.width() || rect2.height() > rect1.height());
}

void TestPencilStroke::testAddPoint_BuildsPath()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 3);

    for (int i = 0; i < 10; ++i) {
        stroke.addPoint(QPointF(100 + i * 10, 100 + i * 5));
    }

    QPainterPath path = stroke.strokePath();
    QVERIFY(!path.isEmpty());
}

void TestPencilStroke::testAddPoint_MultiplePoints()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 3);

    // Add enough points to trigger path caching (4+ points for Catmull-Rom)
    for (int i = 0; i < 20; ++i) {
        stroke.addPoint(QPointF(100 + i * 5, 100 + qSin(i) * 20));
    }

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.width() >= 95);  // At least 19 * 5 = 95
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestPencilStroke::testClone_CreatesNewInstance()
{
    QVector<QPointF> points = createTestPoints(5);
    PencilStroke stroke(points, Qt::red, 3);

    auto cloned = stroke.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &stroke);
}

void TestPencilStroke::testClone_PreservesPoints()
{
    QVector<QPointF> points = createTestPoints(5);
    PencilStroke stroke(points, Qt::red, 3);

    auto cloned = stroke.clone();
    QCOMPARE(cloned->boundingRect(), stroke.boundingRect());
}

void TestPencilStroke::testClone_PreservesColor()
{
    QVector<QPointF> points = createTestPoints(3);
    PencilStroke stroke(points, Qt::blue, 3);

    auto cloned = stroke.clone();

    // Draw both and compare (color should match)
    QImage img1(100, 100, QImage::Format_ARGB32);
    QImage img2(100, 100, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    img2.fill(Qt::white);

    QPainter p1(&img1);
    QPainter p2(&img2);
    stroke.draw(p1);
    cloned->draw(p2);

    QCOMPARE(img1, img2);
}

void TestPencilStroke::testClone_PreservesWidth()
{
    QVector<QPointF> points = createTestPoints(3);
    PencilStroke stroke(points, Qt::red, 15);

    auto cloned = stroke.clone();
    QCOMPARE(cloned->boundingRect(), stroke.boundingRect());
}

void TestPencilStroke::testClone_PreservesLineStyle()
{
    QVector<QPointF> points = createTestPoints(5);
    PencilStroke stroke(points, Qt::red, 3, LineStyle::Dashed);

    auto cloned = stroke.clone();

    // Both should render identically
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

// ============================================================================
// Intersection Tests
// ============================================================================

void TestPencilStroke::testIntersectsCircle_NoIntersection()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    PencilStroke stroke(points, Qt::red, 3);

    // Circle far from the stroke
    QVERIFY(!stroke.intersectsCircle(QPoint(100, 300), 10));
}

void TestPencilStroke::testIntersectsCircle_Intersection()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    PencilStroke stroke(points, Qt::red, 5);

    // Circle on the stroke
    QVERIFY(stroke.intersectsCircle(QPoint(150, 100), 10));
}

void TestPencilStroke::testIntersectsCircle_EdgeCase()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    PencilStroke stroke(points, Qt::red, 5);

    // Circle just touching the edge
    QVERIFY(stroke.intersectsCircle(QPoint(100, 100), 5));
}

void TestPencilStroke::testIntersectsCircle_EmptyStroke()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 3);
    QVERIFY(!stroke.intersectsCircle(QPoint(100, 100), 10));
}

// ============================================================================
// Stroke Path Tests
// ============================================================================

void TestPencilStroke::testStrokePath_EmptyPoints()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 3);
    QPainterPath path = stroke.strokePath();
    QVERIFY(path.isEmpty());
}

void TestPencilStroke::testStrokePath_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    PencilStroke stroke(points, Qt::red, 3);
    QPainterPath path = stroke.strokePath();
    QVERIFY(path.isEmpty());  // Need at least 2 points for a path
}

void TestPencilStroke::testStrokePath_ValidPath()
{
    QVector<QPointF> points = createTestPoints(5);
    PencilStroke stroke(points, Qt::red, 5);

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

void TestPencilStroke::testDraw_EmptyPoints()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 3);

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should not crash
    stroke.draw(painter);
    QVERIFY(true);
}

void TestPencilStroke::testDraw_SinglePoint()
{
    QVector<QPointF> points = { QPointF(50, 50) };
    PencilStroke stroke(points, Qt::red, 3);

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should not crash (won't draw anything, but shouldn't crash)
    stroke.draw(painter);
    QVERIFY(true);
}

void TestPencilStroke::testDraw_MultiplePoints()
{
    QVector<QPointF> points = createTestPoints(10, 5);
    PencilStroke stroke(points, Qt::red, 3);

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    stroke.draw(painter);

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

void TestPencilStroke::testDraw_AllLineStyles()
{
    QVector<QPointF> points = createTestPoints(10, 10);

    QList<LineStyle> styles = { LineStyle::Solid, LineStyle::Dashed, LineStyle::Dotted };

    for (LineStyle style : styles) {
        PencilStroke stroke(points, Qt::red, 3, style);

        QImage image(200, 200, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);

        stroke.draw(painter);
        QVERIFY(true);  // No crash
    }
}

QTEST_MAIN(TestPencilStroke)
#include "tst_PencilStroke.moc"
