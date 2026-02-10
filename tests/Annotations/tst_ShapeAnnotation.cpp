#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "annotations/ShapeAnnotation.h"

/**
 * @brief Tests for ShapeAnnotation class
 *
 * Covers:
 * - Construction with rectangle and ellipse types
 * - Bounding rectangle calculations with stroke width
 * - Fill mode (outline vs filled)
 * - Clone functionality
 * - Rect modification
 * - Drawing verification
 */
class TestShapeAnnotation : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_Rectangle();
    void testConstruction_Ellipse();
    void testConstruction_WithColor();
    void testConstruction_WithWidth();
    void testConstruction_Filled();
    void testConstruction_Outline();

    // Shape type tests
    void testShapeType_Rectangle();
    void testShapeType_Ellipse();

    // Rect accessor tests
    void testRect_Returns();
    void testSetRect_Updates();
    void testSetRect_UpdatesBoundingRect();

    // Bounding rect tests
    void testBoundingRect_IncludesStrokeWidth();
    void testBoundingRect_NormalizedRect();
    void testBoundingRect_NegativeRect();

    // Clone tests
    void testClone_CreatesNewInstance();
    void testClone_PreservesRect();
    void testClone_PreservesType();
    void testClone_PreservesColor();
    void testClone_PreservesWidth();
    void testClone_PreservesFilled();

    // Drawing tests (verify no crash and correct output)
    void testDraw_Rectangle();
    void testDraw_Ellipse();
    void testDraw_FilledRectangle();
    void testDraw_FilledEllipse();
    void testDraw_NegativeRect();
    void testDraw_LargeStrokeWidth();

    // Edge cases
    void testEdgeCase_VeryLargeRect();
    void testEdgeCase_SquareRect();
};

// ============================================================================
// Construction Tests
// ============================================================================

void TestShapeAnnotation::testConstruction_Rectangle()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 3);

    QCOMPARE(shape.shapeType(), ShapeType::Rectangle);
    QCOMPARE(shape.rect(), QRect(10, 10, 100, 50));
}

void TestShapeAnnotation::testConstruction_Ellipse()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Ellipse, Qt::blue, 3);

    QCOMPARE(shape.shapeType(), ShapeType::Ellipse);
    QCOMPARE(shape.rect(), QRect(10, 10, 100, 50));
}

void TestShapeAnnotation::testConstruction_WithColor()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::green, 3);

    // Verify color by drawing
    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    shape.draw(painter);
    painter.end();

    // Check if green is present (outline)
    bool hasGreen = false;
    for (int y = 0; y < image.height() && !hasGreen; ++y) {
        for (int x = 0; x < image.width() && !hasGreen; ++x) {
            QColor c = image.pixelColor(x, y);
            if (c.green() > c.red() && c.green() > c.blue() && c.green() > 200) {
                hasGreen = true;
            }
        }
    }
    QVERIFY(hasGreen);
}

void TestShapeAnnotation::testConstruction_WithWidth()
{
    ShapeAnnotation thinShape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 1);
    ShapeAnnotation thickShape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 20);

    // Thick shape should have larger bounding rect
    QVERIFY(thickShape.boundingRect().width() > thinShape.boundingRect().width());
    QVERIFY(thickShape.boundingRect().height() > thinShape.boundingRect().height());
}

void TestShapeAnnotation::testConstruction_Filled()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 3, true);

    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    shape.draw(painter);
    painter.end();

    // Check center of shape is filled (red)
    QColor centerColor = image.pixelColor(60, 35);
    QVERIFY(centerColor.red() > 200);
}

void TestShapeAnnotation::testConstruction_Outline()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 3, false);

    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    shape.draw(painter);
    painter.end();

    // Center should remain white (outline only)
    QColor centerColor = image.pixelColor(60, 35);
    QCOMPARE(centerColor, QColor(Qt::white));
}

// ============================================================================
// Shape Type Tests
// ============================================================================

void TestShapeAnnotation::testShapeType_Rectangle()
{
    ShapeAnnotation shape(QRect(0, 0, 100, 100), ShapeType::Rectangle, Qt::red, 3);
    QCOMPARE(shape.shapeType(), ShapeType::Rectangle);
    QCOMPARE(static_cast<int>(shape.shapeType()), 0);
}

void TestShapeAnnotation::testShapeType_Ellipse()
{
    ShapeAnnotation shape(QRect(0, 0, 100, 100), ShapeType::Ellipse, Qt::red, 3);
    QCOMPARE(shape.shapeType(), ShapeType::Ellipse);
    QCOMPARE(static_cast<int>(shape.shapeType()), 1);
}

// ============================================================================
// Rect Accessor Tests
// ============================================================================

void TestShapeAnnotation::testRect_Returns()
{
    QRect testRect(25, 35, 150, 200);
    ShapeAnnotation shape(testRect, ShapeType::Rectangle, Qt::red, 3);

    QCOMPARE(shape.rect(), testRect);
}

void TestShapeAnnotation::testSetRect_Updates()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 3);

    QRect newRect(20, 20, 200, 100);
    shape.setRect(newRect);

    QCOMPARE(shape.rect(), newRect);
}

void TestShapeAnnotation::testSetRect_UpdatesBoundingRect()
{
    ShapeAnnotation shape(QRect(10, 10, 50, 50), ShapeType::Rectangle, Qt::red, 3);
    QRect originalBounds = shape.boundingRect();

    shape.setRect(QRect(10, 10, 200, 200));
    QRect newBounds = shape.boundingRect();

    QVERIFY(newBounds.width() > originalBounds.width());
    QVERIFY(newBounds.height() > originalBounds.height());
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestShapeAnnotation::testBoundingRect_IncludesStrokeWidth()
{
    QRect shapeRect(100, 100, 50, 50);
    int strokeWidth = 10;
    ShapeAnnotation shape(shapeRect, ShapeType::Rectangle, Qt::red, strokeWidth);

    QRect bounds = shape.boundingRect();

    // Bounding rect should be larger than shape rect by half stroke width on each side
    int margin = strokeWidth / 2 + 1;
    QVERIFY(bounds.left() <= shapeRect.left() - margin + 1);
    QVERIFY(bounds.top() <= shapeRect.top() - margin + 1);
    QVERIFY(bounds.right() >= shapeRect.right() + margin - 1);
    QVERIFY(bounds.bottom() >= shapeRect.bottom() + margin - 1);
}

void TestShapeAnnotation::testBoundingRect_NormalizedRect()
{
    // Rect with negative width/height
    QRect invertedRect(100, 100, -50, -50);
    ShapeAnnotation shape(invertedRect, ShapeType::Rectangle, Qt::red, 3);

    QRect bounds = shape.boundingRect();
    QVERIFY(bounds.width() > 0);
    QVERIFY(bounds.height() > 0);
}

void TestShapeAnnotation::testBoundingRect_NegativeRect()
{
    QRect negRect(150, 150, -100, -100);
    ShapeAnnotation shape(negRect, ShapeType::Ellipse, Qt::red, 3);

    QRect bounds = shape.boundingRect();
    QVERIFY(bounds.isValid() || !bounds.isEmpty());
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestShapeAnnotation::testClone_CreatesNewInstance()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 3);

    auto cloned = shape.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &shape);
}

void TestShapeAnnotation::testClone_PreservesRect()
{
    QRect testRect(25, 35, 150, 200);
    ShapeAnnotation shape(testRect, ShapeType::Rectangle, Qt::red, 3);

    auto cloned = shape.clone();
    auto* clonedShape = dynamic_cast<ShapeAnnotation*>(cloned.get());

    QVERIFY(clonedShape != nullptr);
    QCOMPARE(clonedShape->rect(), testRect);
}

void TestShapeAnnotation::testClone_PreservesType()
{
    ShapeAnnotation ellipse(QRect(10, 10, 100, 50), ShapeType::Ellipse, Qt::red, 3);

    auto cloned = ellipse.clone();
    auto* clonedShape = dynamic_cast<ShapeAnnotation*>(cloned.get());

    QVERIFY(clonedShape != nullptr);
    QCOMPARE(clonedShape->shapeType(), ShapeType::Ellipse);
}

void TestShapeAnnotation::testClone_PreservesColor()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::blue, 3);

    auto cloned = shape.clone();

    // Draw both and compare
    QImage img1(150, 100, QImage::Format_ARGB32);
    QImage img2(150, 100, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    img2.fill(Qt::white);

    QPainter p1(&img1);
    QPainter p2(&img2);
    shape.draw(p1);
    cloned->draw(p2);

    QCOMPARE(img1, img2);
}

void TestShapeAnnotation::testClone_PreservesWidth()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 15);

    auto cloned = shape.clone();
    QCOMPARE(cloned->boundingRect(), shape.boundingRect());
}

void TestShapeAnnotation::testClone_PreservesFilled()
{
    ShapeAnnotation filledShape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 3, true);

    auto cloned = filledShape.clone();

    // Both should render identically (filled)
    QImage img1(150, 100, QImage::Format_ARGB32);
    QImage img2(150, 100, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    img2.fill(Qt::white);

    QPainter p1(&img1);
    QPainter p2(&img2);
    filledShape.draw(p1);
    cloned->draw(p2);

    QCOMPARE(img1, img2);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestShapeAnnotation::testDraw_Rectangle()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Rectangle, Qt::red, 3);

    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    shape.draw(painter);
    painter.end();

    // Verify something was drawn
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixelColor(x, y) != Qt::white) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

void TestShapeAnnotation::testDraw_Ellipse()
{
    ShapeAnnotation shape(QRect(10, 10, 100, 50), ShapeType::Ellipse, Qt::blue, 3);

    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    shape.draw(painter);
    painter.end();

    // Verify something was drawn
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixelColor(x, y) != Qt::white) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

void TestShapeAnnotation::testDraw_FilledRectangle()
{
    ShapeAnnotation shape(QRect(20, 20, 60, 40), ShapeType::Rectangle, Qt::green, 3, true);

    QImage image(100, 80, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    shape.draw(painter);
    painter.end();

    // Center of filled rectangle should be green
    QColor centerColor = image.pixelColor(50, 40);
    QVERIFY(centerColor.green() > 200);
}

void TestShapeAnnotation::testDraw_FilledEllipse()
{
    ShapeAnnotation shape(QRect(20, 20, 60, 40), ShapeType::Ellipse, Qt::cyan, 3, true);

    QImage image(100, 80, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    shape.draw(painter);
    painter.end();

    // Center of filled ellipse should be cyan
    QColor centerColor = image.pixelColor(50, 40);
    QVERIFY(centerColor.cyan() > 200 || (centerColor.green() > 200 && centerColor.blue() > 200));
}

void TestShapeAnnotation::testDraw_NegativeRect()
{
    // Rect defined from bottom-right to top-left
    ShapeAnnotation shape(QRect(100, 80, -60, -40), ShapeType::Rectangle, Qt::red, 3);

    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should normalize and draw correctly
    shape.draw(painter);

    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixelColor(x, y) != Qt::white) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

void TestShapeAnnotation::testDraw_LargeStrokeWidth()
{
    ShapeAnnotation shape(QRect(50, 50, 100, 100), ShapeType::Rectangle, Qt::red, 30);

    QImage image(250, 250, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    shape.draw(painter);

    // Verify thick stroke is visible
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixelColor(x, y) != Qt::white) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

// ============================================================================
// Edge Cases
// ============================================================================

void TestShapeAnnotation::testEdgeCase_VeryLargeRect()
{
    ShapeAnnotation shape(QRect(0, 0, 10000, 10000), ShapeType::Ellipse, Qt::blue, 5);

    // Just verify bounding rect calculation doesn't overflow
    QRect bounds = shape.boundingRect();
    QVERIFY(bounds.width() > 10000);
    QVERIFY(bounds.height() > 10000);
}

void TestShapeAnnotation::testEdgeCase_SquareRect()
{
    ShapeAnnotation rect(QRect(10, 10, 100, 100), ShapeType::Rectangle, Qt::red, 3);
    ShapeAnnotation ellipse(QRect(10, 10, 100, 100), ShapeType::Ellipse, Qt::red, 3);

    // Both should have same bounding rect for square
    QCOMPARE(rect.boundingRect(), ellipse.boundingRect());
}

QTEST_MAIN(TestShapeAnnotation)
#include "tst_ShapeAnnotation.moc"
