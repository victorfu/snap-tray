#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWidget>
#include <QScreen>
#include "toolbar/ToolOptionsPanel.h"

namespace {
constexpr int kWidthSectionSize = 28;
constexpr int kWidthToColorSpacing = -2;
constexpr int kSectionSpacing = 8;
constexpr int kSwatchSize = 20;        // Larger swatches
constexpr int kSwatchSpacing = 3;      // Slightly more spacing
constexpr int kColorPadding = 4;
constexpr int kStandardColorCount = 6; // 6 standard colors
// Grid width = 1 custom swatch + 6 standard colors = 7 items in single row
constexpr int kColorItemCount = 1 + kStandardColorCount;  // Custom + standard colors
constexpr int kColorGridWidth = kColorItemCount * kSwatchSize + (kColorItemCount - 1) * kSwatchSpacing;
constexpr int kColorSectionWidth = kColorGridWidth + kColorPadding * 2;
constexpr int kWidgetRightMargin = 6;
constexpr int kArrowSectionWidth = 52;
constexpr int kArrowButtonOffset = kArrowSectionWidth / 2;
constexpr int kVerticalPadding = 4;
// Widget has minimum height based on other sections (28 + 2*4 = 36)
constexpr int kWidgetHeight = 28 + 2 * kVerticalPadding;
}

class TestToolOptionsPanelHitTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Widget contains tests
    void testContainsInMainWidget();
    void testContainsOutsideWidget();
    void testContainsInDropdownWhenOpen();
    void testSharedModeContainsEmbeddedPopupWhenOpen();

    // Layout position tests
    void testUpdatePositionBelow();
    void testUpdatePositionAbove();
    void testUpdatePositionLeftBoundary();
    void testUpdatePositionRightBoundary();
    void testSharedModeAnchorPreservedBelow();
    void testSharedModeAnchorPreservedAbove();

    // Bounding rect tests
    void testBoundingRectWithColorOnly();
    void testBoundingRectWithColorAndWidth();
    void testBoundingRectWithAllSections();

private:
    QWidget* m_host = nullptr;
    ToolOptionsPanel* m_widget = nullptr;

    // Helper to set up widget at a standard position
    void setupWidgetAtPosition();
    void setupSharedWidget(bool above = false);
    bool canRunSharedWidgetTests() const;
    QQuickWidget* sharedQuickWidget(const QString& objectName) const;
    QObject* sharedLoadedSection(QQuickWidget* quickWidget) const;
    QPoint sharedButtonCenter(QQuickWidget* quickWidget) const;
    QPoint sharedFirstOptionCenter(QQuickWidget* quickWidget) const;
};

void TestToolOptionsPanelHitTest::init()
{
    m_widget = new ToolOptionsPanel();
}

void TestToolOptionsPanelHitTest::cleanup()
{
    if (m_host) {
        delete m_host;
        m_host = nullptr;
        m_widget = nullptr;
        return;
    }

    delete m_widget;
    m_widget = nullptr;
}

void TestToolOptionsPanelHitTest::setupWidgetAtPosition()
{
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);
}

void TestToolOptionsPanelHitTest::setupSharedWidget(bool above)
{
    delete m_widget;
    m_host = new QWidget();
    m_host->resize(640, 480);
    m_widget = new ToolOptionsPanel(m_host);
    m_widget->setUseSharedStyleDropdowns(true);
    m_widget->setShowArrowStyleSection(true);
    m_widget->setShowLineStyleSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, above ? 200 : 100, 200, 40), above, 1920);
    QCoreApplication::processEvents();
}

bool TestToolOptionsPanelHitTest::canRunSharedWidgetTests() const
{
    auto* screen = QGuiApplication::primaryScreen();
    return screen && screen->geometry().isValid() && !screen->geometry().isEmpty() &&
           QGuiApplication::screenAt(screen->geometry().center()) != nullptr;
}

QQuickWidget* TestToolOptionsPanelHitTest::sharedQuickWidget(const QString& objectName) const
{
    return m_host->findChild<QQuickWidget*>(objectName);
}

QObject* TestToolOptionsPanelHitTest::sharedLoadedSection(QQuickWidget* quickWidget) const
{
    auto* root = qobject_cast<QQuickItem*>(quickWidget ? quickWidget->rootObject() : nullptr);
    return root ? qvariant_cast<QObject*>(root->property("loadedSection")) : nullptr;
}

QPoint TestToolOptionsPanelHitTest::sharedButtonCenter(QQuickWidget* quickWidget) const
{
    auto* section = sharedLoadedSection(quickWidget);
    Q_ASSERT(section);
    const int sectionX = qRound(section->property("x").toDouble());
    const int buttonWidth = section->property("buttonWidth").toInt();
    const int buttonHeight = section->property("buttonHeight").toInt();
    const int buttonTop = section->property("buttonTop").toInt();
    return QPoint(sectionX + buttonWidth / 2, buttonTop + buttonHeight / 2);
}

QPoint TestToolOptionsPanelHitTest::sharedFirstOptionCenter(QQuickWidget* quickWidget) const
{
    auto* section = sharedLoadedSection(quickWidget);
    Q_ASSERT(section);

    const int sectionX = qRound(section->property("x").toDouble());
    const int buttonWidth = section->property("buttonWidth").toInt();
    const int buttonHeight = section->property("buttonHeight").toInt();
    const int buttonTop = section->property("buttonTop").toInt();
    const int dropdownWidth = section->property("dropdownWidth").toInt();
    const int dropdownGap = section->property("dropdownGap").toInt();
    const int dropdownPadding = section->property("dropdownPadding").toInt();
    const int dropdownSpacing = section->property("dropdownSpacing").toInt();
    const int dropdownOptionHeight = section->property("dropdownOptionHeight").toInt();
    const bool expandUpward = section->property("expandUpward").toBool();
    const int dropdownX = sectionX + buttonWidth - dropdownWidth;
    const int dropdownY = expandUpward ? buttonTop : buttonTop + buttonHeight + dropdownGap;
    const int optionsY = dropdownY + dropdownPadding + dropdownOptionHeight / 2 + dropdownSpacing / 2;
    return QPoint(dropdownX + dropdownWidth / 2, optionsY);
}

// ============================================================================
// Widget Contains Tests
// ============================================================================

void TestToolOptionsPanelHitTest::testContainsInMainWidget()
{
    setupWidgetAtPosition();

    QRect widgetRect = m_widget->boundingRect();
    QPoint center = widgetRect.center();

    QVERIFY(m_widget->contains(center));
}

void TestToolOptionsPanelHitTest::testContainsOutsideWidget()
{
    setupWidgetAtPosition();

    QRect widgetRect = m_widget->boundingRect();

    // Point far outside the widget
    QPoint outside(widgetRect.right() + 100, widgetRect.bottom() + 100);

    QVERIFY(!m_widget->contains(outside));
}

void TestToolOptionsPanelHitTest::testContainsInDropdownWhenOpen()
{
    m_widget->setShowArrowStyleSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Layout order: WidthSection -> ColorSection -> ArrowStyleSection
    // WidthSection: SECTION_SIZE = 28
    // ColorSection: grid(7 * 20 + 6 * 3) + padding(4 * 2) = 166
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

void TestToolOptionsPanelHitTest::testSharedModeContainsEmbeddedPopupWhenOpen()
{
    if (!canRunSharedWidgetTests())
        QSKIP("Shared QQuickWidget hit tests require an available screen");

    setupSharedWidget(false);

    auto* arrowQuickWidget = sharedQuickWidget(QStringLiteral("sharedArrowStyleQuickWidget"));
    QVERIFY(arrowQuickWidget != nullptr);

    const QPoint buttonCenter = sharedButtonCenter(arrowQuickWidget);
    const QPoint hostButtonCenter = arrowQuickWidget->mapTo(m_host, buttonCenter);

    QVERIFY(m_widget->handleClick(hostButtonCenter));
    QVERIFY(!m_widget->hasOpenSharedStyleMenu());

    QTest::mouseClick(arrowQuickWidget, Qt::LeftButton, Qt::NoModifier, buttonCenter);
    QTRY_VERIFY(m_widget->hasOpenSharedStyleMenu());

    const QPoint popupHostPoint = arrowQuickWidget->mapTo(m_host, sharedFirstOptionCenter(arrowQuickWidget));
    QVERIFY(m_widget->contains(popupHostPoint));
}

// ============================================================================
// Layout Position Tests
// ============================================================================

void TestToolOptionsPanelHitTest::testUpdatePositionBelow()
{
    m_widget->setVisible(true);
    QRect anchorRect(100, 100, 200, 40);
    m_widget->updatePosition(anchorRect, false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Widget should be positioned below the anchor
    QVERIFY(widgetRect.top() > anchorRect.bottom());
    QCOMPARE(widgetRect.top(), anchorRect.bottom() + 4);
}

void TestToolOptionsPanelHitTest::testUpdatePositionAbove()
{
    m_widget->setVisible(true);
    QRect anchorRect(100, 200, 200, 40);  // Anchor lower on screen
    m_widget->updatePosition(anchorRect, true, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Widget should be positioned above the anchor
    QVERIFY(widgetRect.bottom() < anchorRect.top());
    QCOMPARE(widgetRect.bottom(), anchorRect.top() - 4 - 1);  // -1 for bottom() vs height
}

void TestToolOptionsPanelHitTest::testUpdatePositionLeftBoundary()
{
    m_widget->setVisible(true);
    // Anchor near left edge
    QRect anchorRect(-50, 100, 100, 40);
    m_widget->updatePosition(anchorRect, false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Widget should not go past left boundary (minimum X = 5)
    QVERIFY(widgetRect.left() >= 5);
}

void TestToolOptionsPanelHitTest::testUpdatePositionRightBoundary()
{
    m_widget->setVisible(true);
    // Anchor near right edge of a narrow screen
    QRect anchorRect(700, 100, 100, 40);
    m_widget->updatePosition(anchorRect, false, 800);  // Narrow screen

    QRect widgetRect = m_widget->boundingRect();

    // Widget should not go past right boundary
    QVERIFY(widgetRect.right() <= 800 - 5);
}

void TestToolOptionsPanelHitTest::testSharedModeAnchorPreservedBelow()
{
    if (!canRunSharedWidgetTests())
        QSKIP("Shared QQuickWidget hit tests require an available screen");

    setupSharedWidget(false);

    auto* arrowQuickWidget = sharedQuickWidget(QStringLiteral("sharedArrowStyleQuickWidget"));
    QVERIFY(arrowQuickWidget != nullptr);

    QRect widgetRect = m_widget->boundingRect();
    const QPoint expectedButtonCenter(widgetRect.left() + kWidthSectionSize + kWidthToColorSpacing +
                                          kColorSectionWidth + kSectionSpacing + kArrowButtonOffset,
                                      widgetRect.center().y());

    const QPoint actualButtonCenter = arrowQuickWidget->mapTo(m_host, sharedButtonCenter(arrowQuickWidget));
    QCOMPARE(actualButtonCenter, expectedButtonCenter);

    QTest::mouseClick(arrowQuickWidget, Qt::LeftButton, Qt::NoModifier, sharedButtonCenter(arrowQuickWidget));
    QTRY_VERIFY(m_widget->hasOpenSharedStyleMenu());
    QCOMPARE(arrowQuickWidget->mapTo(m_host, sharedButtonCenter(arrowQuickWidget)), expectedButtonCenter);
}

void TestToolOptionsPanelHitTest::testSharedModeAnchorPreservedAbove()
{
    if (!canRunSharedWidgetTests())
        QSKIP("Shared QQuickWidget hit tests require an available screen");

    setupSharedWidget(true);

    auto* arrowQuickWidget = sharedQuickWidget(QStringLiteral("sharedArrowStyleQuickWidget"));
    QVERIFY(arrowQuickWidget != nullptr);

    QRect widgetRect = m_widget->boundingRect();
    const QPoint expectedButtonCenter(widgetRect.left() + kWidthSectionSize + kWidthToColorSpacing +
                                          kColorSectionWidth + kSectionSpacing + kArrowButtonOffset,
                                      widgetRect.center().y());

    const QPoint actualButtonCenter = arrowQuickWidget->mapTo(m_host, sharedButtonCenter(arrowQuickWidget));
    QCOMPARE(actualButtonCenter, expectedButtonCenter);

    QTest::mouseClick(arrowQuickWidget, Qt::LeftButton, Qt::NoModifier, sharedButtonCenter(arrowQuickWidget));
    QTRY_VERIFY(m_widget->hasOpenSharedStyleMenu());
    QCOMPARE(arrowQuickWidget->mapTo(m_host, sharedButtonCenter(arrowQuickWidget)), expectedButtonCenter);
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestToolOptionsPanelHitTest::testBoundingRectWithColorOnly()
{
    m_widget->setShowWidthSection(false);
    m_widget->setShowTextSection(false);
    m_widget->setShowArrowStyleSection(false);
    m_widget->setShowShapeSection(false);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 1920);

    QRect widgetRect = m_widget->boundingRect();

    // Color section: grid(7 * 20 + 6 * 3) + padding(4 * 2) = 166
    // Widget right margin = 6
    QCOMPARE(widgetRect.height(), kWidgetHeight);

    // Width should be just the color section + right margin
    QCOMPARE(widgetRect.width(), kColorSectionWidth + kWidgetRightMargin);
}

void TestToolOptionsPanelHitTest::testBoundingRectWithColorAndWidth()
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
    // Width-to-color spacing: -2
    // Color section: grid(7 * 20 + 6 * 3) + padding(4 * 2) = 166
    // Widget right margin = 6
    int expectedWidth = kWidthSectionSize + kWidthToColorSpacing +
                        kColorSectionWidth + kWidgetRightMargin;
    QCOMPARE(widgetRect.width(), expectedWidth);
}

void TestToolOptionsPanelHitTest::testBoundingRectWithAllSections()
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

QTEST_MAIN(TestToolOptionsPanelHitTest)
#include "tst_HitTest.moc"
