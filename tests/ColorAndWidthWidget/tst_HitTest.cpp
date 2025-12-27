#include <QtTest/QtTest>
#include "ColorAndWidthWidget.h"

class TestColorAndWidthWidgetHitTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Widget contains tests
    void testContainsInMainWidget();
    void testContainsOutsideWidget();
    void testContainsInDropdownWhenOpen();

    // Layout position tests
    void testUpdatePositionBelow();
    void testUpdatePositionAbove();
    void testUpdatePositionLeftBoundary();
    void testUpdatePositionRightBoundary();

    // Bounding rect tests
    void testBoundingRectWithColorOnly();
    void testBoundingRectWithColorAndWidth();
    void testBoundingRectWithAllSections();

private:
    ColorAndWidthWidget* m_widget = nullptr;

    // Helper to set up widget at a standard position
    void setupWidgetAtPosition();
};

void TestColorAndWidthWidgetHitTest::init()
{
    m_widget = new ColorAndWidthWidget();
}

void TestColorAndWidthWidgetHitTest::cleanup()
{
    delete m_widget;
    m_widget = nullptr;
}

void TestColorAndWidthWidgetHitTest::setupWidgetAtPosition()
{
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);
}

// ============================================================================
// Widget Contains Tests
// ============================================================================

void TestColorAndWidthWidgetHitTest::testContainsInMainWidget()
{
    setupWidgetAtPosition();

    QRect widgetRect = m_widget->boundingRect();
    QPoint center = widgetRect.center();

    QVERIFY(m_widget->contains(center));
}

void TestColorAndWidthWidgetHitTest::testContainsOutsideWidget()
{
    setupWidgetAtPosition();

    QRect widgetRect = m_widget->boundingRect();

    // Point far outside the widget
    QPoint outside(widgetRect.right() + 100, widgetRect.bottom() + 100);

    QVERIFY(!m_widget->contains(outside));
}

void TestColorAndWidthWidgetHitTest::testContainsInDropdownWhenOpen()
{
    m_widget->setShowArrowStyleSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // First, click on the arrow style button to open the dropdown
    // The arrow style button is after color section and width section
    // ColorSection: 8 * 16 + 7 * 2 + 6 * 2 = 154
    int colorSectionWidth = 8 * 16 + 7 * 2 + 6 * 2;  // 154
    // WidthSection: SECTION_SIZE = 36
    int widthSectionWidth = 36;
    // SECTION_SPACING = 8
    int sectionSpacing = 8;
    // Arrow button is at xOffset + TOGGLE_WIDTH(28) + TOGGLE_SPACING(8), center is at + BUTTON_WIDTH(42)/2
    int arrowButtonOffset = 28 + 8 + 21;  // polyline toggle + spacing + button center

    int arrowButtonX = widgetRect.left() + colorSectionWidth + sectionSpacing + widthSectionWidth + sectionSpacing + arrowButtonOffset;
    int arrowButtonY = widgetRect.center().y();

    m_widget->handleClick(QPoint(arrowButtonX, arrowButtonY));

    // Now the dropdown should be open
    // The dropdown appears below the button
    // Check that a point in the dropdown area is contained
    QPoint dropdownPoint(arrowButtonX, widgetRect.bottom() + 20);

    // The contains method should return true for dropdown area when open
    QVERIFY(m_widget->contains(dropdownPoint));
}

// ============================================================================
// Layout Position Tests
// ============================================================================

void TestColorAndWidthWidgetHitTest::testUpdatePositionBelow()
{
    m_widget->setVisible(true);
    QRect anchorRect(100, 100, 200, 40);
    m_widget->updatePosition(anchorRect, false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Widget should be positioned below the anchor
    QVERIFY(widgetRect.top() > anchorRect.bottom());
    QCOMPARE(widgetRect.top(), anchorRect.bottom() + 4);
}

void TestColorAndWidthWidgetHitTest::testUpdatePositionAbove()
{
    m_widget->setVisible(true);
    QRect anchorRect(100, 200, 200, 40);  // Anchor lower on screen
    m_widget->updatePosition(anchorRect, true, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Widget should be positioned above the anchor
    QVERIFY(widgetRect.bottom() < anchorRect.top());
    QCOMPARE(widgetRect.bottom(), anchorRect.top() - 4 - 1);  // -1 for bottom() vs height
}

void TestColorAndWidthWidgetHitTest::testUpdatePositionLeftBoundary()
{
    m_widget->setVisible(true);
    // Anchor near left edge
    QRect anchorRect(-50, 100, 100, 40);
    m_widget->updatePosition(anchorRect, false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Widget should not go past left boundary (minimum X = 5)
    QVERIFY(widgetRect.left() >= 5);
}

void TestColorAndWidthWidgetHitTest::testUpdatePositionRightBoundary()
{
    m_widget->setVisible(true);
    // Anchor near right edge of a narrow screen
    QRect anchorRect(700, 100, 100, 40);
    m_widget->updatePosition(anchorRect, false, 800);  // Narrow screen

    QRect widgetRect = m_widget->boundingRect();

    // Widget should not go past right boundary
    QVERIFY(widgetRect.right() <= 800 - 5);
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestColorAndWidthWidgetHitTest::testBoundingRectWithColorOnly()
{
    m_widget->setShowWidthSection(false);
    m_widget->setShowTextSection(false);
    m_widget->setShowArrowStyleSection(false);
    m_widget->setShowShapeSection(false);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Color section: 8 swatches * 16px + 7 gaps * 2px + 2 * padding (6px) = 154
    // Widget height is fixed at 36
    // Widget right margin = 6
    QCOMPARE(widgetRect.height(), 36);

    // Width should be just the color section + right margin
    int expectedColorWidth = 8 * 16 + 7 * 2 + 2 * 6;  // 154
    int widgetRightMargin = 6;
    QCOMPARE(widgetRect.width(), expectedColorWidth + widgetRightMargin);
}

void TestColorAndWidthWidgetHitTest::testBoundingRectWithColorAndWidth()
{
    m_widget->setShowWidthSection(true);
    m_widget->setShowTextSection(false);
    m_widget->setShowArrowStyleSection(false);
    m_widget->setShowShapeSection(false);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Color section: 154
    // Width section: spacing (8) + size (36) = 44
    // Widget right margin = 6
    int colorSectionWidth = 8 * 16 + 7 * 2 + 2 * 6;  // 154
    int widthSectionWidth = 8 + 36;  // 44
    int widgetRightMargin = 6;
    int expectedWidth = colorSectionWidth + widthSectionWidth + widgetRightMargin;

    QCOMPARE(widgetRect.width(), expectedWidth);
}

void TestColorAndWidthWidgetHitTest::testBoundingRectWithAllSections()
{
    m_widget->setShowWidthSection(true);
    m_widget->setShowTextSection(true);
    m_widget->setShowArrowStyleSection(true);
    m_widget->setShowShapeSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Widget should be wider with all sections
    // Just verify it's larger than with color only
    int colorOnlyWidth = 8 * 16 + 7 * 2 + 2 * 6;  // 142
    QVERIFY(widgetRect.width() > colorOnlyWidth);

    // Height should still be fixed
    QCOMPARE(widgetRect.height(), 36);
}

QTEST_MAIN(TestColorAndWidthWidgetHitTest)
#include "tst_HitTest.moc"
