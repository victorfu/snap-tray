#include <QtTest/QtTest>
#include "toolbar/ToolOptionsPanel.h"

class TestToolOptionsPanelState : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Default state tests
    void testDefaultConstructorState();
    void testDefaultColor();
    void testDefaultWidth();
    void testDefaultVisibility();
    void testDefaultTextFormatting();
    void testDefaultArrowStyle();
    void testDefaultShapeSettings();

    // Color tests
    void testSetCurrentColor();
    void testSetColors();

    // Width tests
    void testSetCurrentWidth();
    void testSetCurrentWidthClamping();
    void testSetWidthRange();
    void testSetWidthRangeAdjustsCurrentWidth();

    // Text formatting tests
    void testSetBold();
    void testSetItalic();
    void testSetUnderline();
    void testSetFontSize();
    void testSetFontFamily();

    // Arrow style tests
    void testSetArrowStyle();
    void testSetShowArrowStyleSection();

    // Shape tests
    void testSetShapeType();
    void testSetShapeFillMode();
    void testSetShowShapeSection();

    // Section visibility tests
    void testSetShowWidthSection();
    void testSetShowTextSection();
    void testSetVisible();

private:
    ToolOptionsPanel* m_widget = nullptr;
};

void TestToolOptionsPanelState::initTestCase()
{
}

void TestToolOptionsPanelState::cleanupTestCase()
{
}

void TestToolOptionsPanelState::init()
{
    m_widget = new ToolOptionsPanel();
}

void TestToolOptionsPanelState::cleanup()
{
    delete m_widget;
    m_widget = nullptr;
}

// ============================================================================
// Default State Tests
// ============================================================================

void TestToolOptionsPanelState::testDefaultConstructorState()
{
    QVERIFY(m_widget != nullptr);
}

void TestToolOptionsPanelState::testDefaultColor()
{
    QCOMPARE(m_widget->currentColor(), QColor(Qt::red));
}

void TestToolOptionsPanelState::testDefaultWidth()
{
    QCOMPARE(m_widget->currentWidth(), 3);
}

void TestToolOptionsPanelState::testDefaultVisibility()
{
    QCOMPARE(m_widget->isVisible(), false);
}

void TestToolOptionsPanelState::testDefaultTextFormatting()
{
    // Bold defaults to true (as per implementation)
    QCOMPARE(m_widget->isBold(), true);
    QCOMPARE(m_widget->isItalic(), false);
    QCOMPARE(m_widget->isUnderline(), false);
    QCOMPARE(m_widget->fontSize(), 16);
}

void TestToolOptionsPanelState::testDefaultArrowStyle()
{
    QCOMPARE(m_widget->arrowStyle(), LineEndStyle::EndArrow);
    QCOMPARE(m_widget->showArrowStyleSection(), false);
}

void TestToolOptionsPanelState::testDefaultShapeSettings()
{
    QCOMPARE(m_widget->shapeType(), ShapeType::Rectangle);
    QCOMPARE(m_widget->shapeFillMode(), ShapeFillMode::Outline);
    QCOMPARE(m_widget->showShapeSection(), false);
}

// ============================================================================
// Color Tests
// ============================================================================

void TestToolOptionsPanelState::testSetCurrentColor()
{
    QColor testColor(Qt::blue);
    m_widget->setCurrentColor(testColor);
    QCOMPARE(m_widget->currentColor(), testColor);
}

void TestToolOptionsPanelState::testSetColors()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue };
    m_widget->setColors(colors);
    // The colors are set internally; verify by checking current color still works
    m_widget->setCurrentColor(Qt::green);
    QCOMPARE(m_widget->currentColor(), QColor(Qt::green));
}

// ============================================================================
// Width Tests
// ============================================================================

void TestToolOptionsPanelState::testSetCurrentWidth()
{
    m_widget->setCurrentWidth(10);
    QCOMPARE(m_widget->currentWidth(), 10);
}

void TestToolOptionsPanelState::testSetCurrentWidthClamping()
{
    // Default range is 1-20
    m_widget->setCurrentWidth(0);  // Below minimum
    QCOMPARE(m_widget->currentWidth(), 1);

    m_widget->setCurrentWidth(100);  // Above maximum
    QCOMPARE(m_widget->currentWidth(), 20);
}

void TestToolOptionsPanelState::testSetWidthRange()
{
    m_widget->setWidthRange(5, 50);
    m_widget->setCurrentWidth(25);
    QCOMPARE(m_widget->currentWidth(), 25);

    // Test clamping with new range
    m_widget->setCurrentWidth(3);  // Below new minimum
    QCOMPARE(m_widget->currentWidth(), 5);

    m_widget->setCurrentWidth(60);  // Above new maximum
    QCOMPARE(m_widget->currentWidth(), 50);
}

void TestToolOptionsPanelState::testSetWidthRangeAdjustsCurrentWidth()
{
    m_widget->setCurrentWidth(15);
    QCOMPARE(m_widget->currentWidth(), 15);

    // Set a range that excludes current width
    m_widget->setWidthRange(1, 10);
    QCOMPARE(m_widget->currentWidth(), 10);  // Should be clamped to max
}

// ============================================================================
// Text Formatting Tests
// ============================================================================

void TestToolOptionsPanelState::testSetBold()
{
    m_widget->setBold(false);
    QCOMPARE(m_widget->isBold(), false);

    m_widget->setBold(true);
    QCOMPARE(m_widget->isBold(), true);
}

void TestToolOptionsPanelState::testSetItalic()
{
    m_widget->setItalic(true);
    QCOMPARE(m_widget->isItalic(), true);

    m_widget->setItalic(false);
    QCOMPARE(m_widget->isItalic(), false);
}

void TestToolOptionsPanelState::testSetUnderline()
{
    m_widget->setUnderline(true);
    QCOMPARE(m_widget->isUnderline(), true);

    m_widget->setUnderline(false);
    QCOMPARE(m_widget->isUnderline(), false);
}

void TestToolOptionsPanelState::testSetFontSize()
{
    m_widget->setFontSize(24);
    QCOMPARE(m_widget->fontSize(), 24);
}

void TestToolOptionsPanelState::testSetFontFamily()
{
    m_widget->setFontFamily("Helvetica");
    QCOMPARE(m_widget->fontFamily(), QString("Helvetica"));
}

// ============================================================================
// Arrow Style Tests
// ============================================================================

void TestToolOptionsPanelState::testSetArrowStyle()
{
    m_widget->setArrowStyle(LineEndStyle::None);
    QCOMPARE(m_widget->arrowStyle(), LineEndStyle::None);

    m_widget->setArrowStyle(LineEndStyle::EndArrow);
    QCOMPARE(m_widget->arrowStyle(), LineEndStyle::EndArrow);
}

void TestToolOptionsPanelState::testSetShowArrowStyleSection()
{
    m_widget->setShowArrowStyleSection(true);
    QCOMPARE(m_widget->showArrowStyleSection(), true);

    m_widget->setShowArrowStyleSection(false);
    QCOMPARE(m_widget->showArrowStyleSection(), false);
}

// ============================================================================
// Shape Tests
// ============================================================================

void TestToolOptionsPanelState::testSetShapeType()
{
    m_widget->setShapeType(ShapeType::Ellipse);
    QCOMPARE(m_widget->shapeType(), ShapeType::Ellipse);

    m_widget->setShapeType(ShapeType::Rectangle);
    QCOMPARE(m_widget->shapeType(), ShapeType::Rectangle);
}

void TestToolOptionsPanelState::testSetShapeFillMode()
{
    m_widget->setShapeFillMode(ShapeFillMode::Filled);
    QCOMPARE(m_widget->shapeFillMode(), ShapeFillMode::Filled);

    m_widget->setShapeFillMode(ShapeFillMode::Outline);
    QCOMPARE(m_widget->shapeFillMode(), ShapeFillMode::Outline);
}

void TestToolOptionsPanelState::testSetShowShapeSection()
{
    m_widget->setShowShapeSection(true);
    QCOMPARE(m_widget->showShapeSection(), true);

    m_widget->setShowShapeSection(false);
    QCOMPARE(m_widget->showShapeSection(), false);
}

// ============================================================================
// Section Visibility Tests
// ============================================================================

void TestToolOptionsPanelState::testSetShowWidthSection()
{
    // Default is true
    QCOMPARE(m_widget->showWidthSection(), true);

    m_widget->setShowWidthSection(false);
    QCOMPARE(m_widget->showWidthSection(), false);

    m_widget->setShowWidthSection(true);
    QCOMPARE(m_widget->showWidthSection(), true);
}

void TestToolOptionsPanelState::testSetShowTextSection()
{
    // Default is false
    QCOMPARE(m_widget->showTextSection(), false);

    m_widget->setShowTextSection(true);
    QCOMPARE(m_widget->showTextSection(), true);

    m_widget->setShowTextSection(false);
    QCOMPARE(m_widget->showTextSection(), false);
}

void TestToolOptionsPanelState::testSetVisible()
{
    m_widget->setVisible(true);
    QCOMPARE(m_widget->isVisible(), true);

    m_widget->setVisible(false);
    QCOMPARE(m_widget->isVisible(), false);
}

QTEST_MAIN(TestToolOptionsPanelState)
#include "tst_State.moc"
