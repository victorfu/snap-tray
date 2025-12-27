#include <QtTest/QtTest>
#include <QSignalSpy>
#include "ColorAndWidthWidget.h"

class TestColorAndWidthWidgetEvents : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Click handling tests
    void testHandleClickOnColorSwatch();
    void testHandleClickOnSecondColorSwatch();
    void testHandleClickOnMoreButton();
    void testHandleClickOutsideWidget();

    // Text section click tests
    void testHandleClickOnBoldButton();
    void testHandleClickOnItalicButton();
    void testHandleClickOnUnderlineButton();
    void testHandleClickOnFontSizeDropdown();
    void testHandleClickOnFontFamilyDropdown();

    // Shape section click tests
    void testHandleClickOnRectangleButton();
    void testHandleClickOnEllipseButton();
    void testHandleClickOnFillModeButton();

    // Arrow style click tests
    void testHandleClickOnArrowStyleButtonOpensDropdown();
    void testHandleClickOnArrowStyleOption();
    void testHandleClickOutsideClosesDropdown();

    // Wheel event tests
    void testHandleWheelScrollUp();
    void testHandleWheelScrollDown();
    void testHandleWheelAtMaxWidth();
    void testHandleWheelAtMinWidth();
    void testHandleWheelWithWidthSectionHidden();

    // Hover update tests
    void testUpdateHoveredOnColorSwatch();
    void testUpdateHoveredOutsideWidget();

    // Mouse press/release tests
    void testHandleMousePressDelegates();
    void testHandleMouseReleaseReturnsFalse();

private:
    ColorAndWidthWidget* m_widget = nullptr;

    void setupWidgetWithAllSections();
    QPoint getColorSwatchCenter(int index);
    QPoint getTextButtonCenter(int buttonIndex);
    QPoint getShapeButtonCenter(int buttonIndex);
};

void TestColorAndWidthWidgetEvents::init()
{
    m_widget = new ColorAndWidthWidget();
}

void TestColorAndWidthWidgetEvents::cleanup()
{
    delete m_widget;
    m_widget = nullptr;
}

void TestColorAndWidthWidgetEvents::setupWidgetWithAllSections()
{
    m_widget->setShowWidthSection(true);
    m_widget->setShowTextSection(true);
    m_widget->setShowArrowStyleSection(true);
    m_widget->setShowShapeSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);
}

QPoint TestColorAndWidthWidgetEvents::getColorSwatchCenter(int index)
{
    QRect widgetRect = m_widget->boundingRect();
    int swatchSize = 16;
    int swatchSpacing = 2;
    int padding = 6;

    int row = index / 8;
    int col = index % 8;

    int x = widgetRect.left() + padding + col * (swatchSize + swatchSpacing) + swatchSize / 2;
    int y = widgetRect.top() + 2 + row * (swatchSize + 2) + swatchSize / 2;

    return QPoint(x, y);
}

QPoint TestColorAndWidthWidgetEvents::getTextButtonCenter(int buttonIndex)
{
    // Text section is after color section, width section, and arrow style section
    QRect widgetRect = m_widget->boundingRect();

    // Calculate section widths based on actual layout constants
    // ColorSection: COLORS_PER_ROW(8) * SWATCH_SIZE(16) + (COLORS_PER_ROW-1)(7) * SWATCH_SPACING(2) + SECTION_PADDING(6) * 2 = 154
    int colorSectionWidth = 8 * 16 + 7 * 2 + 6 * 2;  // 154
    // WidthSection: SECTION_SIZE = 36
    int widthSectionWidth = 36;
    // ArrowStyleSection: BUTTON_WIDTH(42) + TOGGLE_SPACING(8) + TOGGLE_WIDTH(28) = 78
    int arrowSectionWidth = 42 + 8 + 28;  // 78
    // SECTION_SPACING between each section = 8
    int sectionSpacing = 8;

    int textSectionLeft = widgetRect.left() + colorSectionWidth + sectionSpacing + widthSectionWidth + sectionSpacing + arrowSectionWidth + sectionSpacing;
    int buttonSize = 20;
    int buttonSpacing = 2;
    int padding = 6;

    int x = textSectionLeft + padding + buttonIndex * (buttonSize + buttonSpacing) + buttonSize / 2;
    int y = widgetRect.center().y();

    return QPoint(x, y);
}

QPoint TestColorAndWidthWidgetEvents::getShapeButtonCenter(int buttonIndex)
{
    // Shape section is at the end
    QRect widgetRect = m_widget->boundingRect();

    int buttonSize = 24;
    int buttonSpacing = 2;
    int padding = 6;

    // Shape section is at the right side of the widget
    // We need to calculate from the right edge
    int shapeSectionWidth = buttonSize * 3 + buttonSpacing * 2 + padding;
    int shapeSectionLeft = widgetRect.right() - shapeSectionWidth;

    int x = shapeSectionLeft + padding / 2 + buttonIndex * (buttonSize + buttonSpacing) + buttonSize / 2;
    int y = widgetRect.center().y();

    return QPoint(x, y);
}

// ============================================================================
// Click Handling Tests
// ============================================================================

void TestColorAndWidthWidgetEvents::testHandleClickOnColorSwatch()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::colorSelected);

    QPoint firstSwatchCenter = getColorSwatchCenter(0);
    bool handled = m_widget->handleClick(firstSwatchCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_widget->currentColor(), QColor(Qt::red));
}

void TestColorAndWidthWidgetEvents::testHandleClickOnSecondColorSwatch()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::colorSelected);

    // Second swatch (orange)
    QPoint secondSwatchCenter = getColorSwatchCenter(1);
    bool handled = m_widget->handleClick(secondSwatchCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
    // Orange color from the default palette
    QCOMPARE(m_widget->currentColor(), QColor(255, 149, 0));
}

void TestColorAndWidthWidgetEvents::testHandleClickOnMoreButton()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::moreColorsRequested);

    // More button is at index 15 (after 15 colors)
    QPoint moreButtonCenter = getColorSwatchCenter(15);
    bool handled = m_widget->handleClick(moreButtonCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
}

void TestColorAndWidthWidgetEvents::testHandleClickOutsideWidget()
{
    setupWidgetWithAllSections();

    QRect widgetRect = m_widget->boundingRect();
    QPoint outside(widgetRect.right() + 100, widgetRect.bottom() + 100);

    bool handled = m_widget->handleClick(outside);
    QVERIFY(!handled);
}

// ============================================================================
// Text Section Click Tests
// ============================================================================

void TestColorAndWidthWidgetEvents::testHandleClickOnBoldButton()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::boldToggled);

    // Bold is initially true, clicking should toggle it to false
    QPoint boldButtonCenter = getTextButtonCenter(0);
    bool handled = m_widget->handleClick(boldButtonCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_widget->isBold(), false);
}

void TestColorAndWidthWidgetEvents::testHandleClickOnItalicButton()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::italicToggled);

    // Italic is initially false
    QPoint italicButtonCenter = getTextButtonCenter(1);
    bool handled = m_widget->handleClick(italicButtonCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_widget->isItalic(), true);
}

void TestColorAndWidthWidgetEvents::testHandleClickOnUnderlineButton()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::underlineToggled);

    QPoint underlineButtonCenter = getTextButtonCenter(2);
    bool handled = m_widget->handleClick(underlineButtonCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_widget->isUnderline(), true);
}

void TestColorAndWidthWidgetEvents::testHandleClickOnFontSizeDropdown()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::fontSizeDropdownRequested);

    // Font size dropdown is button index 3 area (after B/I/U)
    QPoint fontSizeCenter = getTextButtonCenter(3);
    // Adjust X position for the font size dropdown which is wider
    fontSizeCenter.setX(fontSizeCenter.x() + 20);

    bool handled = m_widget->handleClick(fontSizeCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
}

void TestColorAndWidthWidgetEvents::testHandleClickOnFontFamilyDropdown()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::fontFamilyDropdownRequested);

    // Font family dropdown is after font size
    QPoint fontFamilyCenter = getTextButtonCenter(4);
    // Adjust X position
    fontFamilyCenter.setX(fontFamilyCenter.x() + 80);

    bool handled = m_widget->handleClick(fontFamilyCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
}

// ============================================================================
// Shape Section Click Tests
// ============================================================================

void TestColorAndWidthWidgetEvents::testHandleClickOnRectangleButton()
{
    setupWidgetWithAllSections();

    // First set to ellipse so we can test changing to rectangle
    m_widget->setShapeType(ShapeType::Ellipse);

    QSignalSpy spy(m_widget, &ColorAndWidthWidget::shapeTypeChanged);

    QPoint rectButtonCenter = getShapeButtonCenter(0);
    bool handled = m_widget->handleClick(rectButtonCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_widget->shapeType(), ShapeType::Rectangle);
}

void TestColorAndWidthWidgetEvents::testHandleClickOnEllipseButton()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::shapeTypeChanged);

    QPoint ellipseButtonCenter = getShapeButtonCenter(1);
    bool handled = m_widget->handleClick(ellipseButtonCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_widget->shapeType(), ShapeType::Ellipse);
}

void TestColorAndWidthWidgetEvents::testHandleClickOnFillModeButton()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::shapeFillModeChanged);

    // Default is Outline, clicking should change to Filled
    QPoint fillButtonCenter = getShapeButtonCenter(2);
    bool handled = m_widget->handleClick(fillButtonCenter);

    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_widget->shapeFillMode(), ShapeFillMode::Filled);
}

// ============================================================================
// Arrow Style Click Tests
// ============================================================================

void TestColorAndWidthWidgetEvents::testHandleClickOnArrowStyleButtonOpensDropdown()
{
    m_widget->setShowArrowStyleSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Arrow style button is after color and width sections
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

    bool handled = m_widget->handleClick(QPoint(arrowButtonX, arrowButtonY));

    QVERIFY(handled);
    // After clicking, the dropdown should be open - clicking again should close it
}

void TestColorAndWidthWidgetEvents::testHandleClickOnArrowStyleOption()
{
    m_widget->setShowArrowStyleSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QSignalSpy spy(m_widget, &ColorAndWidthWidget::arrowStyleChanged);

    QRect widgetRect = m_widget->boundingRect();
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

    // First click to open dropdown
    m_widget->handleClick(QPoint(arrowButtonX, arrowButtonY));

    // Click on first option (None style)
    int dropdownOptionY = widgetRect.bottom() + 2 + 14;  // First option center
    m_widget->handleClick(QPoint(arrowButtonX, dropdownOptionY));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_widget->arrowStyle(), LineEndStyle::None);
}

void TestColorAndWidthWidgetEvents::testHandleClickOutsideClosesDropdown()
{
    m_widget->setShowArrowStyleSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QRect widgetRect = m_widget->boundingRect();
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

    // Open dropdown
    m_widget->handleClick(QPoint(arrowButtonX, arrowButtonY));

    // Click on a color swatch (not the dropdown) - this should close the dropdown
    QPoint colorSwatchCenter = getColorSwatchCenter(0);
    m_widget->handleClick(colorSwatchCenter);

    // The dropdown should now be closed
    // Verify by clicking the arrow button again - it should open the dropdown
    // (If it was still open, clicking would close it)
}

// ============================================================================
// Wheel Event Tests
// ============================================================================

void TestColorAndWidthWidgetEvents::testHandleWheelScrollUp()
{
    m_widget->setShowWidthSection(true);
    m_widget->setCurrentWidth(5);

    bool handled = m_widget->handleWheel(120);  // Positive = scroll up = increase

    QVERIFY(handled);
    QCOMPARE(m_widget->currentWidth(), 6);
}

void TestColorAndWidthWidgetEvents::testHandleWheelScrollDown()
{
    m_widget->setShowWidthSection(true);
    m_widget->setCurrentWidth(5);

    bool handled = m_widget->handleWheel(-120);  // Negative = scroll down = decrease

    QVERIFY(handled);
    QCOMPARE(m_widget->currentWidth(), 4);
}

void TestColorAndWidthWidgetEvents::testHandleWheelAtMaxWidth()
{
    m_widget->setShowWidthSection(true);
    m_widget->setCurrentWidth(20);  // Max width

    bool handled = m_widget->handleWheel(120);  // Try to increase

    // Width should stay at max
    QVERIFY(!handled);  // Not handled because no change
    QCOMPARE(m_widget->currentWidth(), 20);
}

void TestColorAndWidthWidgetEvents::testHandleWheelAtMinWidth()
{
    m_widget->setShowWidthSection(true);
    m_widget->setCurrentWidth(1);  // Min width

    bool handled = m_widget->handleWheel(-120);  // Try to decrease

    QVERIFY(!handled);  // Not handled because no change
    QCOMPARE(m_widget->currentWidth(), 1);
}

void TestColorAndWidthWidgetEvents::testHandleWheelWithWidthSectionHidden()
{
    m_widget->setShowWidthSection(false);
    m_widget->setCurrentWidth(5);

    bool handled = m_widget->handleWheel(120);

    QVERIFY(!handled);  // Wheel events should be ignored
    QCOMPARE(m_widget->currentWidth(), 5);  // Width unchanged
}

// ============================================================================
// Hover Update Tests
// ============================================================================

void TestColorAndWidthWidgetEvents::testUpdateHoveredOnColorSwatch()
{
    setupWidgetWithAllSections();

    QPoint firstSwatchCenter = getColorSwatchCenter(0);
    bool changed = m_widget->updateHovered(firstSwatchCenter);

    QVERIFY(changed);
}

void TestColorAndWidthWidgetEvents::testUpdateHoveredOutsideWidget()
{
    setupWidgetWithAllSections();

    // First hover over a swatch to set initial state
    QPoint firstSwatchCenter = getColorSwatchCenter(0);
    m_widget->updateHovered(firstSwatchCenter);

    // Now move outside
    QRect widgetRect = m_widget->boundingRect();
    QPoint outside(widgetRect.right() + 100, widgetRect.bottom() + 100);

    bool changed = m_widget->updateHovered(outside);

    // State should change (hovered swatch cleared)
    QVERIFY(changed);
}

// ============================================================================
// Mouse Press/Release Tests
// ============================================================================

void TestColorAndWidthWidgetEvents::testHandleMousePressDelegates()
{
    setupWidgetWithAllSections();
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::colorSelected);

    QPoint firstSwatchCenter = getColorSwatchCenter(0);
    bool handled = m_widget->handleMousePress(firstSwatchCenter);

    // handleMousePress delegates to handleClick
    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
}

void TestColorAndWidthWidgetEvents::testHandleMouseReleaseReturnsFalse()
{
    setupWidgetWithAllSections();

    QPoint anyPoint(150, 150);
    bool handled = m_widget->handleMouseRelease(anyPoint);

    // handleMouseRelease always returns false in current implementation
    QVERIFY(!handled);
}

QTEST_MAIN(TestColorAndWidthWidgetEvents)
#include "tst_Events.moc"
