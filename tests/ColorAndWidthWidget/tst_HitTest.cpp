#include <QtTest/QtTest>
#include "ColorAndWidthWidget.h"

namespace {
constexpr int kWidthSectionSize = 28;
constexpr int kWidthToColorSpacing = -2;
constexpr int kSectionSpacing = 8;
constexpr int kSwatchSize = 14;
constexpr int kSwatchSpacing = 2;
constexpr int kColorPadding = 6;
constexpr int kColorRows = 2;
constexpr int kColorRowSpacing = 0;
constexpr int kColorGridWidth = kSwatchSize * 8 + kSwatchSpacing * 7;
constexpr int kColorGridHeight = kColorRows * kSwatchSize +
                                 (kColorRows - 1) * kColorRowSpacing;
constexpr int kColorSectionWidth = kColorGridWidth + kColorPadding * 2;
constexpr int kWidgetRightMargin = 6;
constexpr int kArrowSectionWidth = 52;
constexpr int kArrowButtonOffset = kArrowSectionWidth / 2;
constexpr int kVerticalPadding = 4;
constexpr int kWidgetHeight = kColorGridHeight + 2 * kVerticalPadding;  // 28 + 8 = 36
}

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

    // Layout order: WidthSection -> ColorSection -> ArrowStyleSection
    // WidthSection: SECTION_SIZE = 28
    // ColorSection: grid(8 * 14 + 7 * 2) + padding(6 * 2) = 138
    // Arrow section: button (52), center at 52/2 = 26
    int arrowButtonX = widgetRect.left() + kWidthSectionSize + kWidthToColorSpacing +
                       kColorSectionWidth + kSectionSpacing + kArrowButtonOffset;
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

    // Color section: grid(8 * 14 + 7 * 2) + padding(6 * 2) = 138
    // Widget right margin = 6
    QCOMPARE(widgetRect.height(), kWidgetHeight);

    // Width should be just the color section + right margin
    QCOMPARE(widgetRect.width(), kColorSectionWidth + kWidgetRightMargin);
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

    // Layout order: WidthSection (left) -> ColorSection
    // Width section: size (28)
    // Width-to-color spacing: 2
    // Color section: grid(8 * 14 + 7 * 2) + padding(6 * 2) = 138
    // Widget right margin = 6
    int expectedWidth = kWidthSectionSize + kWidthToColorSpacing +
                        kColorSectionWidth + kWidgetRightMargin;
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
    int colorOnlyWidth = kColorSectionWidth;
    QVERIFY(widgetRect.width() > colorOnlyWidth);

    // Height should match the widget height with padding
    QCOMPARE(widgetRect.height(), kWidgetHeight);
}

QTEST_MAIN(TestColorAndWidthWidgetHitTest)
#include "tst_HitTest.moc"
