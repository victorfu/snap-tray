#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include <QFont>
#include "annotations/TextBoxAnnotation.h"

/**
 * @brief Tests for TextBoxAnnotation class
 *
 * Covers:
 * - Construction with various parameters
 * - Text manipulation
 * - Position and box manipulation
 * - Font and color settings
 * - Rotation and scale transformations
 * - Bounding rectangle calculations
 * - Clone functionality
 * - Hit testing
 * - Drawing tests
 */
class TestTextBoxAnnotation : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_Basic();
    void testConstruction_WithFont();
    void testConstruction_WithColor();

    // Text manipulation tests
    void testSetText();
    void testText_Empty();
    void testText_Multiline();

    // Position tests
    void testSetPosition();
    void testMoveBy_QPointF();
    void testMoveBy_QPoint();
    void testPosition();
    void testCenter();

    // Box tests
    void testBox();

    // Font tests
    void testSetFont();
    void testFont();

    // Color tests
    void testSetColor();
    void testColor();

    // Rotation tests
    void testSetRotation();
    void testRotation_Default();
    void testRotation_90Degrees();
    void testRotation_Negative();

    // Scale tests
    void testSetScale();
    void testScale_Default();

    // Mirror tests
    void testSetMirror();
    void testMirror_Default();

    // Bounding rect tests
    void testBoundingRect_Basic();
    void testBoundingRect_WithRotation();
    void testBoundingRect_WithScale();

    // Geometry tests
    void testTransformedBoundingPolygon();
    void testContainsPoint_Inside();
    void testContainsPoint_Outside();
    void testMapLocalPointToTransformed_NoTransform();
    void testMapLocalPointToTransformed_WithTransform();
    void testTopLeftFromTransformedLocalPoint_RoundTrip();

    // Clone tests
    void testClone_CreatesNewInstance();
    void testClone_PreservesText();
    void testClone_PreservesPosition();
    void testClone_PreservesFont();
    void testClone_PreservesColor();
    void testClone_PreservesTransform();
    void testClone_PreservesMirror();

    // Drawing tests
    void testDraw_Basic();
    void testDraw_WithRotation();
    void testDraw_WithScale();
    void testDraw_EmptyText();
    void testDraw_MultilineText();

    // Constants tests
    void testConstants();
};

// ============================================================================
// Construction Tests
// ============================================================================

void TestTextBoxAnnotation::testConstruction_Basic()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QCOMPARE(textBox.text(), QString("Test"));
    QCOMPARE(textBox.position(), QPointF(100, 100));
    QVERIFY(!textBox.boundingRect().isEmpty());
}

void TestTextBoxAnnotation::testConstruction_WithFont()
{
    QFont font("Times", 20, QFont::Bold);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::black);

    QCOMPARE(textBox.font().family(), QString("Times"));
    QCOMPARE(textBox.font().pointSize(), 20);
}

void TestTextBoxAnnotation::testConstruction_WithColor()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::blue);

    QCOMPARE(textBox.color(), QColor(Qt::blue));
}

// ============================================================================
// Text Manipulation Tests
// ============================================================================

void TestTextBoxAnnotation::testSetText()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Original", font, Qt::red);

    textBox.setText("Modified");
    QCOMPARE(textBox.text(), QString("Modified"));
}

void TestTextBoxAnnotation::testText_Empty()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "", font, Qt::red);

    QCOMPARE(textBox.text(), QString(""));
}

void TestTextBoxAnnotation::testText_Multiline()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Line1\nLine2\nLine3", font, Qt::red);

    QCOMPARE(textBox.text(), QString("Line1\nLine2\nLine3"));
}

// ============================================================================
// Position Tests
// ============================================================================

void TestTextBoxAnnotation::testSetPosition()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.setPosition(QPointF(200, 200));
    QCOMPARE(textBox.position(), QPointF(200, 200));
}

void TestTextBoxAnnotation::testMoveBy_QPointF()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.moveBy(QPointF(50, 50));
    QCOMPARE(textBox.position(), QPointF(150, 150));
}

void TestTextBoxAnnotation::testMoveBy_QPoint()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.moveBy(QPoint(50, 50));
    QCOMPARE(textBox.position(), QPointF(150, 150));
}

void TestTextBoxAnnotation::testPosition()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(123, 456), "Test", font, Qt::red);

    QCOMPARE(textBox.position(), QPointF(123, 456));
}

void TestTextBoxAnnotation::testCenter()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QPointF center = textBox.center();
    QRectF box = textBox.box();

    // Center should be at box center offset by position
    QPointF expectedCenter = textBox.position() + QPointF(box.width() / 2, box.height() / 2);
    QCOMPARE(center, expectedCenter);
}

// ============================================================================
// Box Tests
// ============================================================================

void TestTextBoxAnnotation::testBox()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test text", font, Qt::red);

    QRectF box = textBox.box();
    QVERIFY(box.width() >= TextBoxAnnotation::kMinWidth);
    QVERIFY(box.height() >= TextBoxAnnotation::kMinHeight);
}

// ============================================================================
// Font Tests
// ============================================================================

void TestTextBoxAnnotation::testSetFont()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QFont newFont("Helvetica", 20, QFont::Bold, true);
    textBox.setFont(newFont);

    QCOMPARE(textBox.font().family(), QString("Helvetica"));
    QCOMPARE(textBox.font().pointSize(), 20);
}

void TestTextBoxAnnotation::testFont()
{
    QFont font("Courier", 12);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QCOMPARE(textBox.font().family(), QString("Courier"));
    QCOMPARE(textBox.font().pointSize(), 12);
}

// ============================================================================
// Color Tests
// ============================================================================

void TestTextBoxAnnotation::testSetColor()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.setColor(Qt::green);
    QCOMPARE(textBox.color(), QColor(Qt::green));
}

void TestTextBoxAnnotation::testColor()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::cyan);

    QCOMPARE(textBox.color(), QColor(Qt::cyan));
}

// ============================================================================
// Rotation Tests
// ============================================================================

void TestTextBoxAnnotation::testSetRotation()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.setRotation(45.0);
    QCOMPARE(textBox.rotation(), 45.0);
}

void TestTextBoxAnnotation::testRotation_Default()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QCOMPARE(textBox.rotation(), 0.0);
}

void TestTextBoxAnnotation::testRotation_90Degrees()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.setRotation(90.0);
    QCOMPARE(textBox.rotation(), 90.0);
}

void TestTextBoxAnnotation::testRotation_Negative()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.setRotation(-45.0);
    QCOMPARE(textBox.rotation(), -45.0);
}

// ============================================================================
// Scale Tests
// ============================================================================

void TestTextBoxAnnotation::testSetScale()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.setScale(2.0);
    QCOMPARE(textBox.scale(), 2.0);
}

void TestTextBoxAnnotation::testScale_Default()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QCOMPARE(textBox.scale(), 1.0);
}

// ============================================================================
// Mirror Tests
// ============================================================================

void TestTextBoxAnnotation::testSetMirror()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    textBox.setMirror(true, false);
    QCOMPARE(textBox.mirrorX(), true);
    QCOMPARE(textBox.mirrorY(), false);

    textBox.setMirror(false, true);
    QCOMPARE(textBox.mirrorX(), false);
    QCOMPARE(textBox.mirrorY(), true);
}

void TestTextBoxAnnotation::testMirror_Default()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QCOMPARE(textBox.mirrorX(), false);
    QCOMPARE(textBox.mirrorY(), false);
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestTextBoxAnnotation::testBoundingRect_Basic()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QRect rect = textBox.boundingRect();
    QVERIFY(!rect.isEmpty());
    QVERIFY(rect.contains(QPoint(100, 100)));
}

void TestTextBoxAnnotation::testBoundingRect_WithRotation()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QRect rectNoRotation = textBox.boundingRect();

    textBox.setRotation(45.0);
    QRect rectWithRotation = textBox.boundingRect();

    // Rotated bounding rect should generally be larger
    QVERIFY(!rectWithRotation.isEmpty());
}

void TestTextBoxAnnotation::testBoundingRect_WithScale()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QRect rectNoScale = textBox.boundingRect();

    textBox.setScale(2.0);
    QRect rectWithScale = textBox.boundingRect();

    // Scaled bounding rect should be larger
    QVERIFY(rectWithScale.width() >= rectNoScale.width());
    QVERIFY(rectWithScale.height() >= rectNoScale.height());
}

// ============================================================================
// Geometry Tests
// ============================================================================

void TestTextBoxAnnotation::testTransformedBoundingPolygon()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QPolygonF polygon = textBox.transformedBoundingPolygon();
    QCOMPARE(polygon.size(), 4);  // Rectangle has 4 corners
}

void TestTextBoxAnnotation::testContainsPoint_Inside()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    // Point inside the text box (with some offset for the text area)
    QVERIFY(textBox.containsPoint(QPoint(110, 110)));
}

void TestTextBoxAnnotation::testContainsPoint_Outside()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    // Point far outside the text box
    QVERIFY(!textBox.containsPoint(QPoint(500, 500)));
}

void TestTextBoxAnnotation::testMapLocalPointToTransformed_NoTransform()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    QPointF localPoint(12.0, 20.0);
    QPointF mapped = textBox.mapLocalPointToTransformed(localPoint);
    QCOMPARE(mapped, QPointF(112.0, 120.0));
}

void TestTextBoxAnnotation::testMapLocalPointToTransformed_WithTransform()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);
    textBox.setRotation(90.0);

    QPointF localPoint(12.0, 20.0);
    QPointF mapped = textBox.mapLocalPointToTransformed(localPoint);

    // A pure 90-degree rotation around center must move the point.
    QVERIFY(!qFuzzyCompare(mapped.x(), 112.0) || !qFuzzyCompare(mapped.y(), 120.0));
}

void TestTextBoxAnnotation::testTopLeftFromTransformedLocalPoint_RoundTrip()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(130, 160), "Round trip", font, Qt::red);
    textBox.setRotation(270.0);
    textBox.setScale(1.5);
    textBox.setMirror(true, false);

    QPointF localPoint(18.0, 24.0);
    QPointF transformed = textBox.mapLocalPointToTransformed(localPoint);

    QPointF recoveredTopLeft = textBox.topLeftFromTransformedLocalPoint(transformed, localPoint);
    QVERIFY(qAbs(recoveredTopLeft.x() - textBox.position().x()) < 0.01);
    QVERIFY(qAbs(recoveredTopLeft.y() - textBox.position().y()) < 0.01);
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestTextBoxAnnotation::testClone_CreatesNewInstance()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    auto cloned = textBox.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &textBox);
}

void TestTextBoxAnnotation::testClone_PreservesText()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Hello World", font, Qt::red);

    auto cloned = textBox.clone();
    auto* clonedTextBox = dynamic_cast<TextBoxAnnotation*>(cloned.get());

    QVERIFY(clonedTextBox != nullptr);
    QCOMPARE(clonedTextBox->text(), QString("Hello World"));
}

void TestTextBoxAnnotation::testClone_PreservesPosition()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(150, 200), "Test", font, Qt::red);

    auto cloned = textBox.clone();
    auto* clonedTextBox = dynamic_cast<TextBoxAnnotation*>(cloned.get());

    QVERIFY(clonedTextBox != nullptr);
    QCOMPARE(clonedTextBox->position(), QPointF(150, 200));
}

void TestTextBoxAnnotation::testClone_PreservesFont()
{
    QFont font("Courier", 18, QFont::Bold);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);

    auto cloned = textBox.clone();
    auto* clonedTextBox = dynamic_cast<TextBoxAnnotation*>(cloned.get());

    QVERIFY(clonedTextBox != nullptr);
    QCOMPARE(clonedTextBox->font().family(), QString("Courier"));
    QCOMPARE(clonedTextBox->font().pointSize(), 18);
}

void TestTextBoxAnnotation::testClone_PreservesColor()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::magenta);

    auto cloned = textBox.clone();
    auto* clonedTextBox = dynamic_cast<TextBoxAnnotation*>(cloned.get());

    QVERIFY(clonedTextBox != nullptr);
    QCOMPARE(clonedTextBox->color(), QColor(Qt::magenta));
}

void TestTextBoxAnnotation::testClone_PreservesTransform()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);
    textBox.setRotation(30.0);
    textBox.setScale(1.5);

    auto cloned = textBox.clone();
    auto* clonedTextBox = dynamic_cast<TextBoxAnnotation*>(cloned.get());

    QVERIFY(clonedTextBox != nullptr);
    QCOMPARE(clonedTextBox->rotation(), 30.0);
    QCOMPARE(clonedTextBox->scale(), 1.5);
}

void TestTextBoxAnnotation::testClone_PreservesMirror()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Test", font, Qt::red);
    textBox.setMirror(true, true);

    auto cloned = textBox.clone();
    auto* clonedTextBox = dynamic_cast<TextBoxAnnotation*>(cloned.get());

    QVERIFY(clonedTextBox != nullptr);
    QCOMPARE(clonedTextBox->mirrorX(), true);
    QCOMPARE(clonedTextBox->mirrorY(), true);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestTextBoxAnnotation::testDraw_Basic()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(50, 50), "Test", font, Qt::red);

    QImage image(200, 150, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    textBox.draw(painter);
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

void TestTextBoxAnnotation::testDraw_WithRotation()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "Rotated", font, Qt::blue);
    textBox.setRotation(45.0);

    QImage image(300, 300, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    textBox.draw(painter);
    QVERIFY(true);  // No crash
}

void TestTextBoxAnnotation::testDraw_WithScale()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(50, 50), "Scaled", font, Qt::green);
    textBox.setScale(2.0);

    QImage image(300, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    textBox.draw(painter);
    QVERIFY(true);  // No crash
}

void TestTextBoxAnnotation::testDraw_EmptyText()
{
    QFont font("Arial", 14);
    TextBoxAnnotation textBox(QPointF(100, 100), "", font, Qt::red);

    QImage image(200, 150, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should not crash even with empty text
    textBox.draw(painter);
    QVERIFY(true);
}

void TestTextBoxAnnotation::testDraw_MultilineText()
{
    QFont font("Arial", 12);
    TextBoxAnnotation textBox(QPointF(50, 50), "Line 1\nLine 2\nLine 3", font, Qt::black);

    QImage image(300, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    textBox.draw(painter);
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

// ============================================================================
// Constants Tests
// ============================================================================

void TestTextBoxAnnotation::testConstants()
{
    QVERIFY(TextBoxAnnotation::kMinWidth > 0);
    QVERIFY(TextBoxAnnotation::kMinHeight > 0);
    QVERIFY(TextBoxAnnotation::kDefaultWidth >= TextBoxAnnotation::kMinWidth);
    QVERIFY(TextBoxAnnotation::kPadding >= 0);
    QVERIFY(TextBoxAnnotation::kHitMargin >= 0);
}

QTEST_MAIN(TestTextBoxAnnotation)
#include "tst_TextBoxAnnotation.moc"
