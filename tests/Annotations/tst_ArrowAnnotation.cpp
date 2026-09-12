#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "annotations/ArrowAnnotation.h"

/**
 * @brief Tests for ArrowAnnotation class
 *
 * Covers:
 * - Construction with various parameters
 * - Bounding rectangle calculations
 * - Point manipulation (start, end, control)
 * - Clone functionality
 * - Hit testing (containsPoint)
 * - Curve detection (isCurved)
 * - Line end styles
 * - Drawing tests
 */
class TestArrowAnnotation : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_BasicArrow();
    void testConstruction_WithColor();
    void testConstruction_WithWidth();
    void testConstruction_WithLineEndStyle();
    void testConstruction_WithLineStyle();

    // Bounding rect tests
    void testBoundingRect_HorizontalArrow();
    void testBoundingRect_VerticalArrow();
    void testBoundingRect_DiagonalArrow();
    void testBoundingRect_IncludesWidth();
    void testBoundingRect_CurvedArrow();

    // Point manipulation tests
    void testSetStart();
    void testSetEnd();
    void testSetControlPoint();
    void testMoveBy();
    void testEndpointSampling_data();
    void testEndpointSampling();

    // Clone tests
    void testClone_CreatesNewInstance();
    void testClone_PreservesPoints();
    void testClone_PreservesColor();
    void testClone_PreservesStyle();

    // Hit testing tests
    void testContainsPoint_OnLine();
    void testContainsPoint_OffLine();
    void testContainsPoint_NearEndpoints();
    void testContainsPoint_CurvedArrow();

    // Curve detection tests
    void testIsCurved_StraightLine();
    void testIsCurved_CurvedLine();

    // Line end style tests
    void testLineEndStyle_None();
    void testLineEndStyle_EndArrow();
    void testLineEndStyle_BothArrow();
    void testSetLineEndStyle();

    // Drawing tests
    void testDraw_BasicArrow();
    void testDraw_CurvedArrow();
};

// ============================================================================
// Construction Tests
// ============================================================================

void TestArrowAnnotation::testConstruction_BasicArrow()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3);

    QCOMPARE(arrow.start(), QPoint(100, 100));
    QCOMPARE(arrow.end(), QPoint(200, 100));
    QVERIFY(!arrow.boundingRect().isEmpty());
}

void TestArrowAnnotation::testConstruction_WithColor()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 200), Qt::blue, 3);

    // Verify by drawing and checking color
    QImage image(300, 300, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    arrow.draw(painter);
    painter.end();

    // Should have some blue pixels
    bool hasBlue = false;
    for (int y = 0; y < image.height() && !hasBlue; ++y) {
        for (int x = 0; x < image.width() && !hasBlue; ++x) {
            QColor pixel = image.pixelColor(x, y);
            if (pixel.blue() > 200 && pixel.red() < 50 && pixel.green() < 50) {
                hasBlue = true;
            }
        }
    }
    QVERIFY(hasBlue);
}

void TestArrowAnnotation::testConstruction_WithWidth()
{
    ArrowAnnotation thinArrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 2);
    ArrowAnnotation thickArrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 20);

    // Both arrows should have valid bounding rects
    QVERIFY(!thinArrow.boundingRect().isEmpty());
    QVERIFY(!thickArrow.boundingRect().isEmpty());
}

void TestArrowAnnotation::testConstruction_WithLineEndStyle()
{
    ArrowAnnotation arrowNone(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::None);
    ArrowAnnotation arrowEnd(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::EndArrow);
    ArrowAnnotation arrowBoth(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::BothArrow);

    QCOMPARE(arrowNone.lineEndStyle(), LineEndStyle::None);
    QCOMPARE(arrowEnd.lineEndStyle(), LineEndStyle::EndArrow);
    QCOMPARE(arrowBoth.lineEndStyle(), LineEndStyle::BothArrow);
}

void TestArrowAnnotation::testConstruction_WithLineStyle()
{
    ArrowAnnotation solidArrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Solid);
    ArrowAnnotation dashedArrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Dashed);
    ArrowAnnotation dottedArrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Dotted);

    // All should have valid bounding rects
    QVERIFY(!solidArrow.boundingRect().isEmpty());
    QVERIFY(!dashedArrow.boundingRect().isEmpty());
    QVERIFY(!dottedArrow.boundingRect().isEmpty());
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestArrowAnnotation::testBoundingRect_HorizontalArrow()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3);
    QRect rect = arrow.boundingRect();

    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(200, 100)));
    QVERIFY(rect.width() >= 100);
}

void TestArrowAnnotation::testBoundingRect_VerticalArrow()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(100, 200), Qt::red, 3);
    QRect rect = arrow.boundingRect();

    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(100, 200)));
    QVERIFY(rect.height() >= 100);
}

void TestArrowAnnotation::testBoundingRect_DiagonalArrow()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 200), Qt::red, 3);
    QRect rect = arrow.boundingRect();

    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(200, 200)));
}

void TestArrowAnnotation::testBoundingRect_IncludesWidth()
{
    ArrowAnnotation thinArrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 2);
    ArrowAnnotation thickArrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 20);

    // Both should have valid bounding rects that include arrow margins
    QVERIFY(!thinArrow.boundingRect().isEmpty());
    QVERIFY(!thickArrow.boundingRect().isEmpty());
    QVERIFY(thickArrow.boundingRect().height() >= 1);  // Has some height
}

void TestArrowAnnotation::testBoundingRect_CurvedArrow()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3);
    arrow.setControlPoint(QPoint(150, 50));  // Curve upward

    QRect rect = arrow.boundingRect();
    // Include the visible bowed shaft; the off-curve control point is not ink.
    QVERIFY(rect.contains(QPoint(150, 75)));
}

// ============================================================================
// Point Manipulation Tests
// ============================================================================

void TestArrowAnnotation::testSetStart()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3);
    arrow.setStart(QPoint(50, 50));

    QCOMPARE(arrow.start(), QPoint(50, 50));
    QVERIFY(arrow.boundingRect().contains(QPoint(50, 50)));
}

void TestArrowAnnotation::testSetEnd()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3);
    arrow.setEnd(QPoint(300, 150));

    QCOMPARE(arrow.end(), QPoint(300, 150));
    QVERIFY(arrow.boundingRect().contains(QPoint(300, 150)));
}

void TestArrowAnnotation::testSetControlPoint()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3);
    arrow.setControlPoint(QPoint(150, 50));

    QCOMPARE(arrow.controlPoint(), QPoint(150, 50));
    QVERIFY(arrow.isCurved());
}

void TestArrowAnnotation::testMoveBy()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 200), Qt::red, 3);
    QPoint originalStart = arrow.start();
    QPoint originalEnd = arrow.end();

    arrow.moveBy(QPoint(50, 50));

    QCOMPARE(arrow.start(), originalStart + QPoint(50, 50));
    QCOMPARE(arrow.end(), originalEnd + QPoint(50, 50));
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestArrowAnnotation::testEndpointSampling_data()
{
    QTest::addColumn<QPoint>("delta");
    QTest::addColumn<bool>("moveStart");
    for (QPoint delta : {QPoint(1, 1), QPoint(9, -7), QPoint(-9, 7), QPoint(-13, -11)}) {
        for (bool moveStart : {false, true}) {
            QTest::addRow("%d-%d-start-%d", delta.x(), delta.y(), moveStart)
                << delta << moveStart;
        }
    }
}

void TestArrowAnnotation::testEndpointSampling()
{
    QFETCH(QPoint, delta);
    QFETCH(bool, moveStart);
    ArrowAnnotation single(QPoint(40, 41), QPoint(201, 202), Qt::red, 7);
    QCOMPARE(single.controlPoint(), QPointF(120.5, 121.5));
    single.setControlPoint(QPointF(100.5, 60.5));
    ArrowAnnotation sampled = single;
    const QPoint endpoint = moveStart ? single.start() : single.end();
    auto move = [moveStart](ArrowAnnotation& arrow, QPoint position) {
        if (moveStart) arrow.setStart(position);
        else arrow.setEnd(position);
    };
    move(single, endpoint + delta);
    for (int i = 1; i <= 29; ++i) {
        move(sampled, endpoint + delta * i / 29);
    }
    QCOMPARE(sampled.start(), single.start());
    QCOMPARE(sampled.end(), single.end());
    QCOMPARE(sampled.controlPoint(), single.controlPoint());
    QCOMPARE(single.controlPoint(), QPointF(100.5, 60.5) + QPointF(delta) / 2.0);
    auto render = [](const AnnotationItem& item) {
        QImage image(300, 300, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        item.draw(painter);
        return image;
    };
    QCOMPARE(render(single), render(sampled));
    auto copy = sampled.clone();
    QCOMPARE(static_cast<ArrowAnnotation*>(copy.get())->controlPoint(), single.controlPoint());
    QCOMPARE(render(*copy), render(single));
    move(sampled, endpoint);
    QCOMPARE(sampled.controlPoint(), QPointF(100.5, 60.5));
}

void TestArrowAnnotation::testClone_CreatesNewInstance()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 200), Qt::red, 3);
    auto cloned = arrow.clone();

    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &arrow);
}

void TestArrowAnnotation::testClone_PreservesPoints()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 200), Qt::red, 3);
    arrow.setControlPoint(QPoint(150, 100));

    auto cloned = arrow.clone();
    auto* clonedArrow = dynamic_cast<ArrowAnnotation*>(cloned.get());

    QVERIFY(clonedArrow != nullptr);
    QCOMPARE(clonedArrow->start(), arrow.start());
    QCOMPARE(clonedArrow->end(), arrow.end());
    QCOMPARE(clonedArrow->controlPoint(), arrow.controlPoint());
}

void TestArrowAnnotation::testClone_PreservesColor()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 200), Qt::blue, 3);
    auto cloned = arrow.clone();

    // Draw both and compare
    QImage img1(300, 300, QImage::Format_ARGB32);
    QImage img2(300, 300, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    img2.fill(Qt::white);

    QPainter p1(&img1);
    QPainter p2(&img2);
    arrow.draw(p1);
    cloned->draw(p2);

    QCOMPARE(img1, img2);
}

void TestArrowAnnotation::testClone_PreservesStyle()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 200), Qt::red, 5,
                          LineEndStyle::BothArrow, LineStyle::Dashed);
    auto cloned = arrow.clone();
    auto* clonedArrow = dynamic_cast<ArrowAnnotation*>(cloned.get());

    QVERIFY(clonedArrow != nullptr);
    QCOMPARE(clonedArrow->lineEndStyle(), LineEndStyle::BothArrow);
}

// ============================================================================
// Hit Testing Tests
// ============================================================================

void TestArrowAnnotation::testContainsPoint_OnLine()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 5);

    // Point on the middle of the line
    QVERIFY(arrow.containsPoint(QPoint(150, 100)));
}

void TestArrowAnnotation::testContainsPoint_OffLine()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 5);

    // Point far from the line
    QVERIFY(!arrow.containsPoint(QPoint(150, 200)));
}

void TestArrowAnnotation::testContainsPoint_NearEndpoints()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 5);

    // Points near start and end
    QVERIFY(arrow.containsPoint(QPoint(100, 100)));
    QVERIFY(arrow.containsPoint(QPoint(200, 100)));
}

void TestArrowAnnotation::testContainsPoint_CurvedArrow()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 5);
    arrow.setControlPoint(QPoint(150, 50));

    // Points on the curve endpoints should be detected
    QVERIFY(arrow.containsPoint(QPoint(100, 100)));  // Start point
    QVERIFY(arrow.containsPoint(QPoint(200, 100)));  // End point
    // Note: Curve path detection depends on implementation's hit tolerance
}

// ============================================================================
// Curve Detection Tests
// ============================================================================

void TestArrowAnnotation::testIsCurved_StraightLine()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3);
    // Control point should default to midpoint (straight line)
    QVERIFY(!arrow.isCurved());
}

void TestArrowAnnotation::testIsCurved_CurvedLine()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3);
    arrow.setControlPoint(QPoint(150, 50));  // Off the line

    QVERIFY(arrow.isCurved());
}

// ============================================================================
// Line End Style Tests
// ============================================================================

void TestArrowAnnotation::testLineEndStyle_None()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::None);
    QCOMPARE(arrow.lineEndStyle(), LineEndStyle::None);
}

void TestArrowAnnotation::testLineEndStyle_EndArrow()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::EndArrow);
    QCOMPARE(arrow.lineEndStyle(), LineEndStyle::EndArrow);
}

void TestArrowAnnotation::testLineEndStyle_BothArrow()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::BothArrow);
    QCOMPARE(arrow.lineEndStyle(), LineEndStyle::BothArrow);
}

void TestArrowAnnotation::testSetLineEndStyle()
{
    ArrowAnnotation arrow(QPoint(100, 100), QPoint(200, 100), Qt::red, 3, LineEndStyle::None);
    arrow.setLineEndStyle(LineEndStyle::BothArrowOutline);

    QCOMPARE(arrow.lineEndStyle(), LineEndStyle::BothArrowOutline);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestArrowAnnotation::testDraw_BasicArrow()
{
    ArrowAnnotation arrow(QPoint(50, 50), QPoint(150, 50), Qt::red, 3);

    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    arrow.draw(painter);
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

void TestArrowAnnotation::testDraw_CurvedArrow()
{
    ArrowAnnotation arrow(QPoint(50, 100), QPoint(150, 100), Qt::red, 3);
    arrow.setControlPoint(QPoint(100, 50));

    QImage image(200, 150, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    arrow.draw(painter);
    painter.end();

    // Verify something was drawn in the curve area
    bool hasColorInCurveArea = false;
    for (int y = 40; y < 80 && !hasColorInCurveArea; ++y) {
        for (int x = 80; x < 120 && !hasColorInCurveArea; ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) {
                hasColorInCurveArea = true;
            }
        }
    }
    QVERIFY(hasColorInCurveArea);
}

QTEST_MAIN(TestArrowAnnotation)
#include "tst_ArrowAnnotation.moc"
