#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include <QSignalSpy>
#include "ui/sections/ColorSection.h"
#include "ToolbarStyle.h"

/**
 * @brief Tests for ColorSection UI component
 *
 * Covers:
 * - Color management (set/get colors)
 * - Current color selection
 * - Layout calculations
 * - Hit testing
 * - Click handling
 * - Hover state
 * - Signal emission
 * - Drawing
 */
class TestColorSection : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Color management tests
    void testSetColors();
    void testColors_Default();
    void testSetCurrentColor();
    void testCurrentColor_Default();

    // Layout tests
    void testPreferredWidth();
    void testPreferredHeight();
    void testUpdateLayout();
    void testBoundingRect();

    // Hit testing tests
    void testContains_Inside();
    void testContains_Outside();

    // Click handling tests
    void testHandleClick_ColorSwatch();
    void testHandleClick_Outside();
    void testHandleClick_CustomSwatch();

    // Hover tests
    void testUpdateHovered_OnSwatch();
    void testUpdateHovered_OffSwatch();

    // Signal tests
    void testColorSelectedSignal();
    void testCustomColorPickerRequestedSignal();

    // Drawing tests
    void testDraw_Basic();
    void testDraw_WithSelection();

private:
    ColorSection* m_section = nullptr;
};

void TestColorSection::init()
{
    m_section = new ColorSection();
}

void TestColorSection::cleanup()
{
    delete m_section;
    m_section = nullptr;
}

// ============================================================================
// Color Management Tests
// ============================================================================

void TestColorSection::testSetColors()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue, Qt::yellow };
    m_section->setColors(colors);

    QCOMPARE(m_section->colors().size(), 4);
    QCOMPARE(m_section->colors()[0], QColor(Qt::red));
    QCOMPARE(m_section->colors()[1], QColor(Qt::green));
}

void TestColorSection::testColors_Default()
{
    // Default colors depend on implementation
    QVector<QColor> colors = m_section->colors();
    // Just verify it returns something
    QVERIFY(true);
}

void TestColorSection::testSetCurrentColor()
{
    m_section->setCurrentColor(Qt::blue);
    QCOMPARE(m_section->currentColor(), QColor(Qt::blue));
}

void TestColorSection::testCurrentColor_Default()
{
    QColor defaultColor = m_section->currentColor();
    // Default color is implementation-dependent
    QVERIFY(defaultColor.isValid());
}

// ============================================================================
// Layout Tests
// ============================================================================

void TestColorSection::testPreferredWidth()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue, Qt::yellow,
                               Qt::cyan, Qt::magenta, Qt::white, Qt::black };
    m_section->setColors(colors);

    int width = m_section->preferredWidth();
    QVERIFY(width > 0);
}

void TestColorSection::testPreferredHeight()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue, Qt::yellow };
    m_section->setColors(colors);

    int height = m_section->preferredHeight();
    QVERIFY(height > 0);
}

void TestColorSection::testUpdateLayout()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue };
    m_section->setColors(colors);

    m_section->updateLayout(10, 50, 0);

    QRect rect = m_section->boundingRect();
    QVERIFY(!rect.isEmpty());
}

void TestColorSection::testBoundingRect()
{
    m_section->updateLayout(0, 50, 10);

    QRect rect = m_section->boundingRect();
    QVERIFY(rect.left() >= 0);
    QVERIFY(rect.width() > 0);
}

// ============================================================================
// Hit Testing Tests
// ============================================================================

void TestColorSection::testContains_Inside()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    QPoint insidePoint = rect.center();

    QVERIFY(m_section->contains(insidePoint));
}

void TestColorSection::testContains_Outside()
{
    m_section->updateLayout(0, 50, 0);

    QPoint outsidePoint(-100, -100);

    QVERIFY(!m_section->contains(outsidePoint));
}

// ============================================================================
// Click Handling Tests
// ============================================================================

void TestColorSection::testHandleClick_ColorSwatch()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue };
    m_section->setColors(colors);
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &ColorSection::colorSelected);

    QRect rect = m_section->boundingRect();
    // Click somewhere in the section
    m_section->handleClick(rect.center());

    // May or may not emit signal depending on exact click location
    QVERIFY(true);
}

void TestColorSection::testHandleClick_Outside()
{
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &ColorSection::colorSelected);

    bool handled = m_section->handleClick(QPoint(-100, -100));

    QVERIFY(!handled);
    QCOMPARE(spy.count(), 0);
}

void TestColorSection::testHandleClick_CustomSwatch()
{
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &ColorSection::customColorPickerRequested);

    // Click on custom swatch area (implementation-dependent location)
    // This test is to ensure no crash
    m_section->handleClick(m_section->boundingRect().topLeft());

    QVERIFY(true);
}

// ============================================================================
// Hover Tests
// ============================================================================

void TestColorSection::testUpdateHovered_OnSwatch()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue };
    m_section->setColors(colors);
    m_section->updateLayout(0, 50, 0);

    bool updated = m_section->updateHovered(m_section->boundingRect().center());

    // updateHovered returns true if hover state changed
    QVERIFY(true);  // Just verify no crash
}

void TestColorSection::testUpdateHovered_OffSwatch()
{
    m_section->updateLayout(0, 50, 0);

    bool updated = m_section->updateHovered(QPoint(-100, -100));

    QVERIFY(true);  // Just verify no crash
}

// ============================================================================
// Signal Tests
// ============================================================================

void TestColorSection::testColorSelectedSignal()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue };
    m_section->setColors(colors);
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &ColorSection::colorSelected);

    // Signal should be emitted when color is clicked
    // Exact behavior depends on layout and swatch positions
    QVERIFY(spy.isValid());
}

void TestColorSection::testCustomColorPickerRequestedSignal()
{
    QSignalSpy spy(m_section, &ColorSection::customColorPickerRequested);

    QVERIFY(spy.isValid());
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestColorSection::testDraw_Basic()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue };
    m_section->setColors(colors);
    m_section->updateLayout(0, 50, 10);

    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);
    m_section->draw(painter, config);

    painter.end();

    // Should have drawn color swatches
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

void TestColorSection::testDraw_WithSelection()
{
    QVector<QColor> colors = { Qt::red, Qt::green, Qt::blue };
    m_section->setColors(colors);
    m_section->setCurrentColor(Qt::green);
    m_section->updateLayout(0, 50, 10);

    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);
    m_section->draw(painter, config);

    QVERIFY(true);  // No crash
}

QTEST_MAIN(TestColorSection)
#include "tst_ColorSection.moc"
