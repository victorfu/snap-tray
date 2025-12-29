#include <QtTest/QtTest>
#include <QSignalSpy>
#include "ColorAndWidthWidget.h"

class TestColorAndWidthWidgetSignals : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Color signals
    void testColorSelectedSignal();
    void testMoreColorsRequestedSignal();

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

    // Get the bounding rect and calculate first swatch position
    QRect widgetRect = m_widget->boundingRect();

    // Layout: WidthSection (32px) + spacing (2px) + ColorSection
    // ColorSection starts at: widgetRect.left() + 32 + 2 = widgetRect.left() + 34
    // First swatch is at: colorSectionLeft + padding (6) + swatchSize/2 (7)
    // Swatch size is 14px, so center is at +7
    int widthSectionSize = 32;
    int widthToColorSpacing = 2;
    int colorSectionPadding = 6;
    int swatchSize = 14;

    int colorSectionLeft = widgetRect.left() + widthSectionSize + widthToColorSpacing;
    QPoint firstSwatchCenter(colorSectionLeft + colorSectionPadding + swatchSize / 2,
                             widgetRect.top() + 2 + swatchSize / 2);

    bool handled = m_widget->handleClick(firstSwatchCenter);
    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);

    QList<QVariant> arguments = spy.takeFirst();
    QColor selectedColor = arguments.at(0).value<QColor>();
    QCOMPARE(selectedColor, QColor(Qt::red));
}

void TestColorAndWidthWidgetSignals::testMoreColorsRequestedSignal()
{
    QSignalSpy spy(m_widget, &ColorAndWidthWidget::moreColorsRequested);
    QVERIFY(spy.isValid());

    // Set up the widget
    m_widget->setVisible(true);
    m_widget->setShowMoreButton(true);
    m_widget->updatePosition(QRect(100, 100, 200, 40), false, 800);

    QRect widgetRect = m_widget->boundingRect();

    // Layout: WidthSection (32px) + spacing (2px) + ColorSection
    int widthSectionSize = 32;
    int widthToColorSpacing = 2;
    int colorSectionPadding = 6;
    int colorSectionLeft = widgetRect.left() + widthSectionSize + widthToColorSpacing;

    // The "..." button is at position 15 (after 15 colors, 0-indexed)
    // It's in row 1 (second row), column 7 (8th position, 0-indexed)
    // Position: colorSectionLeft + padding + 7*(14+2), top + 2 + 1*(14+2)
    int swatchSize = 14;
    int swatchSpacing = 2;
    int rowSpacing = 2;
    int moreButtonX = colorSectionLeft + colorSectionPadding + 7 * (swatchSize + swatchSpacing) + swatchSize / 2;
    int moreButtonY = widgetRect.top() + 2 + 1 * (swatchSize + rowSpacing) + swatchSize / 2;

    QPoint moreButtonCenter(moreButtonX, moreButtonY);

    bool handled = m_widget->handleClick(moreButtonCenter);
    QVERIFY(handled);
    QCOMPARE(spy.count(), 1);
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
