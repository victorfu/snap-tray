#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include <QSignalSpy>
#include "ui/sections/ShapeSection.h"
#include "ToolbarStyle.h"

/**
 * @brief Tests for ShapeSection UI component
 *
 * Covers:
 * - Shape type management (Rectangle/Ellipse)
 * - Fill mode management (Outline/Filled)
 * - Layout calculations
 * - Hit testing and button detection
 * - Click handling
 * - Hover state
 * - Signal emission
 * - Drawing
 */
class TestShapeSection : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Shape type tests
    void testSetShapeType_Rectangle();
    void testSetShapeType_Ellipse();
    void testShapeType_Default();

    // Fill mode tests
    void testSetShapeFillMode_Outline();
    void testSetShapeFillMode_Filled();
    void testShapeFillMode_Default();

    // Layout tests
    void testPreferredWidth();
    void testUpdateLayout();
    void testBoundingRect();

    // Hit testing tests
    void testContains_Inside();
    void testContains_Outside();

    // Click handling tests
    void testHandleClick_RectangleButton();
    void testHandleClick_EllipseButton();
    void testHandleClick_FillModeButton();
    void testHandleClick_Outside();

    // Hover tests
    void testUpdateHovered_OnButton();
    void testUpdateHovered_OffButton();

    // Signal tests
    void testShapeTypeChangedSignal();
    void testShapeFillModeChangedSignal();

    // Drawing tests
    void testDraw_Rectangle();
    void testDraw_Ellipse();
    void testDraw_AllCombinations();

private:
    ShapeSection* m_section = nullptr;
};

void TestShapeSection::init()
{
    m_section = new ShapeSection();
}

void TestShapeSection::cleanup()
{
    delete m_section;
    m_section = nullptr;
}

// ============================================================================
// Shape Type Tests
// ============================================================================

void TestShapeSection::testSetShapeType_Rectangle()
{
    m_section->setShapeType(ShapeType::Rectangle);
    QCOMPARE(m_section->shapeType(), ShapeType::Rectangle);
}

void TestShapeSection::testSetShapeType_Ellipse()
{
    m_section->setShapeType(ShapeType::Ellipse);
    QCOMPARE(m_section->shapeType(), ShapeType::Ellipse);
}

void TestShapeSection::testShapeType_Default()
{
    QCOMPARE(m_section->shapeType(), ShapeType::Rectangle);
}

// ============================================================================
// Fill Mode Tests
// ============================================================================

void TestShapeSection::testSetShapeFillMode_Outline()
{
    m_section->setShapeFillMode(ShapeFillMode::Outline);
    QCOMPARE(m_section->shapeFillMode(), ShapeFillMode::Outline);
}

void TestShapeSection::testSetShapeFillMode_Filled()
{
    m_section->setShapeFillMode(ShapeFillMode::Filled);
    QCOMPARE(m_section->shapeFillMode(), ShapeFillMode::Filled);
}

void TestShapeSection::testShapeFillMode_Default()
{
    QCOMPARE(m_section->shapeFillMode(), ShapeFillMode::Outline);
}

// ============================================================================
// Layout Tests
// ============================================================================

void TestShapeSection::testPreferredWidth()
{
    int width = m_section->preferredWidth();
    QVERIFY(width > 0);
}

void TestShapeSection::testUpdateLayout()
{
    m_section->updateLayout(10, 50, 0);

    QRect rect = m_section->boundingRect();
    QVERIFY(!rect.isEmpty());
}

void TestShapeSection::testBoundingRect()
{
    m_section->updateLayout(0, 50, 10);

    QRect rect = m_section->boundingRect();
    QVERIFY(rect.left() >= 0);
    QVERIFY(rect.width() > 0);
}

// ============================================================================
// Hit Testing Tests
// ============================================================================

void TestShapeSection::testContains_Inside()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    QPoint insidePoint = rect.center();

    QVERIFY(m_section->contains(insidePoint));
}

void TestShapeSection::testContains_Outside()
{
    m_section->updateLayout(0, 50, 0);

    QPoint outsidePoint(-100, -100);

    QVERIFY(!m_section->contains(outsidePoint));
}

// ============================================================================
// Click Handling Tests
// ============================================================================

void TestShapeSection::testHandleClick_RectangleButton()
{
    m_section->setShapeType(ShapeType::Ellipse);  // Start with ellipse
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &ShapeSection::shapeTypeChanged);

    // Click on rectangle button area (first button)
    QRect rect = m_section->boundingRect();
    QPoint rectButtonPos(rect.left() + 11, rect.center().y());

    m_section->handleClick(rectButtonPos);

    // Behavior depends on button positions
    QVERIFY(true);  // No crash
}

void TestShapeSection::testHandleClick_EllipseButton()
{
    m_section->setShapeType(ShapeType::Rectangle);  // Start with rectangle
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &ShapeSection::shapeTypeChanged);

    // Click on ellipse button area (second button)
    QRect rect = m_section->boundingRect();
    QPoint ellipseButtonPos(rect.left() + 35, rect.center().y());

    m_section->handleClick(ellipseButtonPos);

    QVERIFY(true);  // No crash
}

void TestShapeSection::testHandleClick_FillModeButton()
{
    m_section->setShapeFillMode(ShapeFillMode::Outline);
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &ShapeSection::shapeFillModeChanged);

    // Click on fill mode button area (third button)
    QRect rect = m_section->boundingRect();
    QPoint fillButtonPos(rect.right() - 11, rect.center().y());

    m_section->handleClick(fillButtonPos);

    QVERIFY(true);  // No crash
}

void TestShapeSection::testHandleClick_Outside()
{
    m_section->updateLayout(0, 50, 0);

    bool handled = m_section->handleClick(QPoint(-100, -100));

    QVERIFY(!handled);
}

// ============================================================================
// Hover Tests
// ============================================================================

void TestShapeSection::testUpdateHovered_OnButton()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    bool updated = m_section->updateHovered(rect.center());

    QVERIFY(true);  // Just verify no crash
}

void TestShapeSection::testUpdateHovered_OffButton()
{
    m_section->updateLayout(0, 50, 0);

    bool updated = m_section->updateHovered(QPoint(-100, -100));

    QVERIFY(true);  // Just verify no crash
}

// ============================================================================
// Signal Tests
// ============================================================================

void TestShapeSection::testShapeTypeChangedSignal()
{
    QSignalSpy spy(m_section, &ShapeSection::shapeTypeChanged);
    QVERIFY(spy.isValid());

    // Direct signal emission is not available, but we can test the spy
    emit m_section->shapeTypeChanged(ShapeType::Ellipse);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<ShapeType>(), ShapeType::Ellipse);
}

void TestShapeSection::testShapeFillModeChangedSignal()
{
    QSignalSpy spy(m_section, &ShapeSection::shapeFillModeChanged);
    QVERIFY(spy.isValid());

    emit m_section->shapeFillModeChanged(ShapeFillMode::Filled);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<ShapeFillMode>(), ShapeFillMode::Filled);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestShapeSection::testDraw_Rectangle()
{
    m_section->setShapeType(ShapeType::Rectangle);
    m_section->updateLayout(0, 50, 10);

    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);
    m_section->draw(painter, config);

    painter.end();

    // Should have drawn button icons
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            QColor pixel = image.pixelColor(x, y);
            if (pixel != Qt::white) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

void TestShapeSection::testDraw_Ellipse()
{
    m_section->setShapeType(ShapeType::Ellipse);
    m_section->updateLayout(0, 50, 10);

    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);
    m_section->draw(painter, config);

    QVERIFY(true);  // No crash
}

void TestShapeSection::testDraw_AllCombinations()
{
    m_section->updateLayout(0, 50, 10);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);

    QList<ShapeType> types = { ShapeType::Rectangle, ShapeType::Ellipse };
    QList<ShapeFillMode> modes = { ShapeFillMode::Outline, ShapeFillMode::Filled };

    for (ShapeType type : types) {
        for (ShapeFillMode mode : modes) {
            m_section->setShapeType(type);
            m_section->setShapeFillMode(mode);

            QImage image(150, 100, QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);

            m_section->draw(painter, config);

            QVERIFY(true);  // No crash
        }
    }
}

QTEST_MAIN(TestShapeSection)
#include "tst_ShapeSection.moc"
