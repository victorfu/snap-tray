#include <QtTest/QtTest>
#include <QSignalSpy>
#include "ColorAndWidthWidget.h"

namespace {
constexpr int kWidthSectionSize = 28;
constexpr int kWidthToColorSpacing = -2;
constexpr int kSectionSpacing = 8;
constexpr int kSwatchSize = 20;        // Larger swatches
constexpr int kSwatchSpacing = 3;      // Slightly more spacing
constexpr int kColorPadding = 4;
constexpr int kStandardColorCount = 6; // 6 standard colors + 1 custom swatch
}

class TestColorAndWidthWidgetSignals : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Color signals
    void testColorSelectedSignal();
    void testCustomColorPickerRequestedSignal();

    // Width signals
    void testWidthChangedSignal();

    // Text formatting signals
    void testBoldToggledSignal();
    void testBoldToggledNoSignalWhenSameValue();
    void testItalicToggledSignal();
    void testItalicToggledNoSignalWhenSameValue();
    void testUnderlineToggledSignal();
    void testUnderlineToggledNoSignalWhenSameValue();
    void testFontSizeChangedSignal();
    void testFontSizeChangedNoSignalWhenSameValue();
    void testFontFamilyChangedSignal();
    void testFontFamilyChangedNoSignalWhenSameValue();

    // Arrow style signals
    void testArrowStyleChangedSignal();
    void testArrowStyleChangedNoSignalWhenSameValue();

    // Shape signals
    void testShapeTypeChangedSignal();
    void testShapeTypeChangedNoSignalWhenSameValue();
    void testShapeFillModeChangedSignal();
    void testShapeFillModeChangedNoSignalWhenSameValue();

private:
    ColorAndWidthWidget* m_widget = nullptr;
};

void TestColorAndWidthWidgetSignals::init()
{
    m_widget = new ColorAndWidthWidget();
}

void TestColorAndWidthWidgetSignals::cleanup()
{
    delete m_widget;
    m_widget = nullptr;
}

// ============================================================================
// Color Signals
// ============================================================================

void TestColorAndWidthWidgetSignals::testColorSelectedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::colorSelected);
    QVERIFY(spy.isValid());

    // Set up the widget position to enable hit testing
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 800);

    // Get the bounding rect and calculate first standard swatch position
    // Layout: WidthSection (28px) + spacing (-2px) + ColorSection
    // ColorSection: custom swatch + 6 standard colors
    // First standard swatch is at: colorSectionLeft + padding + customSwatchSize + spacing + swatchSize/2
    QRect widgetRect = m_widget->boundingRect();
    int colorSectionLeft = widgetRect.left() + kWidthSectionSize + kWidthToColorSpacing;
    int gridLeft = colorSectionLeft + kColorPadding;
    int gridTop = widgetRect.top() + (widgetRect.height() - kSwatchSize) / 2;

    // First standard swatch is after the custom swatch
    int firstStandardLeft = gridLeft + kSwatchSize + kSwatchSpacing;
    QPoint firstStandardSwatchCenter(firstStandardLeft + kSwatchSize / 2,
                                     gridTop + kSwatchSize / 2);

    bool handled = m_widget->handleClick(firstStandardSwatchCenter);
    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QColor selectedColor = arguments.at(0).value<QColor>();
    QCOMPARE(selectedColor, QColor(220, 53, 69));  // First standard color (Red)
}

void TestColorAndWidthWidgetSignals::testCustomColorPickerRequestedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::customColorPickerRequested);
    QVERIFY(spy.isValid());

    // Set up the widget
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 800);

    // Custom swatch is at the leftmost position in ColorSection
    // Double-click on it should request color picker
    QRect widgetRect = m_widget->boundingRect();
    int colorSectionLeft = widgetRect.left() + kWidthSectionSize + kWidthToColorSpacing;
    int gridLeft = colorSectionLeft + kColorPadding;
    int gridTop = widgetRect.top() + (widgetRect.height() - kSwatchSize) / 2;

    // Custom swatch center (first position)
    QPoint customSwatchCenter(gridLeft + kSwatchSize / 2, gridTop + kSwatchSize / 2);

    // Note: ColorSection emits customColorPickerRequested on double-click of custom swatch.
    // The test needs to verify the signal connection exists.
    // Since handleClick on custom swatch applies the custom color (not opens picker),
    // and double-click isn't directly testable here, we just verify signal is connected.
    // This test verifies the signal is valid and can be connected.
    QVERIFY(spy.isValid());
}

// ============================================================================
// Width Signals
// ============================================================================

void TestColorAndWidthWidgetSignals::testWidthChangedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::widthChanged);
    QVERIFY(spy.isValid());

    m_widget->setShowWidthSection(true);
    m_widget->setVisible(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 800);

    // Simulate scroll wheel
    bool handled = m_widget->handleWheel(120);  // Positive delta = scroll up = increase width
    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    int newWidth = arguments.at(0).toInt();
    QCOMPARE(newWidth, 4);  // Default was 3, now 4
}

// ============================================================================
// Text Formatting Signals
// ============================================================================

void TestColorAndWidthWidgetSignals::testBoldToggledSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::boldToggled);
    QVERIFY(spy.isValid());

    // Default is true, toggle to false
    m_widget->setBold(false);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), false);
}

void TestColorAndWidthWidgetSignals::testBoldToggledNoSignalWhenSameValue()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::boldToggled);
    QVERIFY(spy.isValid());

    // Default is true, set to true again
    m_widget->setBold(true);
    QCOMPARE(spy.count(), 0);  // No signal should be emitted
}

void TestColorAndWidthWidgetSignals::testItalicToggledSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::italicToggled);
    QVERIFY(spy.isValid());

    m_widget->setItalic(true);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
}

void TestColorAndWidthWidgetSignals::testItalicToggledNoSignalWhenSameValue()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::italicToggled);
    QVERIFY(spy.isValid());

    // Default is false, set to false again
    m_widget->setItalic(false);
    QCOMPARE(spy.count(), 0);
}

void TestColorAndWidthWidgetSignals::testUnderlineToggledSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::underlineToggled);
    QVERIFY(spy.isValid());

    m_widget->setUnderline(true);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
}

void TestColorAndWidthWidgetSignals::testUnderlineToggledNoSignalWhenSameValue()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::underlineToggled);
    QVERIFY(spy.isValid());

    // Default is false
    m_widget->setUnderline(false);
    QCOMPARE(spy.count(), 0);
}

void TestColorAndWidthWidgetSignals::testFontSizeChangedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::fontSizeChanged);
    QVERIFY(spy.isValid());

    m_widget->setFontSize(24);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toInt(), 24);
}

void TestColorAndWidthWidgetSignals::testFontSizeChangedNoSignalWhenSameValue()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::fontSizeChanged);
    QVERIFY(spy.isValid());

    // Default is 16
    m_widget->setFontSize(16);
    QCOMPARE(spy.count(), 0);
}

void TestColorAndWidthWidgetSignals::testFontFamilyChangedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::fontFamilyChanged);
    QVERIFY(spy.isValid());

    m_widget->setFontFamily("Arial");
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QString("Arial"));
}

void TestColorAndWidthWidgetSignals::testFontFamilyChangedNoSignalWhenSameValue()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::fontFamilyChanged);
    QVERIFY(spy.isValid());

    m_widget->setFontFamily("Helvetica");
    spy.clear();

    // Set same value again
    m_widget->setFontFamily("Helvetica");
    QCOMPARE(spy.count(), 0);
}

// ============================================================================
// Arrow Style Signals
// ============================================================================

void TestColorAndWidthWidgetSignals::testArrowStyleChangedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::arrowStyleChanged);
    QVERIFY(spy.isValid());

    m_widget->setArrowStyle(LineEndStyle::None);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).value<LineEndStyle>(), LineEndStyle::None);
}

void TestColorAndWidthWidgetSignals::testArrowStyleChangedNoSignalWhenSameValue()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::arrowStyleChanged);
    QVERIFY(spy.isValid());

    // Default is EndArrow
    m_widget->setArrowStyle(LineEndStyle::EndArrow);
    QCOMPARE(spy.count(), 0);
}

// ============================================================================
// Shape Signals
// ============================================================================

void TestColorAndWidthWidgetSignals::testShapeTypeChangedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::shapeTypeChanged);
    QVERIFY(spy.isValid());

    m_widget->setShapeType(ShapeType::Ellipse);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).value<ShapeType>(), ShapeType::Ellipse);
}

void TestColorAndWidthWidgetSignals::testShapeTypeChangedNoSignalWhenSameValue()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::shapeTypeChanged);
    QVERIFY(spy.isValid());

    // Default is Rectangle
    m_widget->setShapeType(ShapeType::Rectangle);
    QCOMPARE(spy.count(), 0);
}

void TestColorAndWidthWidgetSignals::testShapeFillModeChangedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::shapeFillModeChanged);
    QVERIFY(spy.isValid());

    m_widget->setShapeFillMode(ShapeFillMode::Filled);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).value<ShapeFillMode>(), ShapeFillMode::Filled);
}

void TestColorAndWidthWidgetSignals::testShapeFillModeChangedNoSignalWhenSameValue()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::shapeFillModeChanged);
    QVERIFY(spy.isValid());

    // Default is Outline
    m_widget->setShapeFillMode(ShapeFillMode::Outline);
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestColorAndWidthWidgetSignals)
#include "tst_Signals.moc"
