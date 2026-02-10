#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include <QSignalSpy>
#include "ui/sections/WidthSection.h"
#include "ToolbarStyle.h"

/**
 * @brief Tests for WidthSection UI component
 *
 * Covers:
 * - Width range management
 * - Current width value
 * - Width clamping
 * - Mouse wheel handling
 * - Layout calculations
 * - Hit testing
 * - Hover state
 * - Signal emission
 * - Drawing
 */
class TestWidthSection : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Width management tests
    void testSetWidthRange();
    void testMinWidth();
    void testMaxWidth();
    void testSetCurrentWidth();
    void testCurrentWidth_Default();
    void testCurrentWidth_Clamping();

    // Wheel event tests
    void testHandleWheel_Increase();
    void testHandleWheel_Decrease();
    void testHandleWheel_AtMax();
    void testHandleWheel_AtMin();

    // Layout tests
    void testPreferredWidth();
    void testUpdateLayout();
    void testBoundingRect();

    // Hit testing tests
    void testContains_Inside();
    void testContains_Outside();

    // Click handling tests
    void testHandleClick();

    // Hover tests
    void testUpdateHovered();

    // Signal tests
    void testWidthChangedSignal();

    // Drawing tests
    void testDraw_Basic();
    void testDraw_DifferentWidths();

private:
    WidthSection* m_section = nullptr;
};

void TestWidthSection::init()
{
    m_section = new WidthSection();
}

void TestWidthSection::cleanup()
{
    delete m_section;
    m_section = nullptr;
}

// ============================================================================
// Width Management Tests
// ============================================================================

void TestWidthSection::testSetWidthRange()
{
    m_section->setWidthRange(5, 50);

    QCOMPARE(m_section->minWidth(), 5);
    QCOMPARE(m_section->maxWidth(), 50);
}

void TestWidthSection::testMinWidth()
{
    m_section->setWidthRange(10, 30);
    QCOMPARE(m_section->minWidth(), 10);
}

void TestWidthSection::testMaxWidth()
{
    m_section->setWidthRange(10, 30);
    QCOMPARE(m_section->maxWidth(), 30);
}

void TestWidthSection::testSetCurrentWidth()
{
    m_section->setWidthRange(1, 20);
    m_section->setCurrentWidth(10);

    QCOMPARE(m_section->currentWidth(), 10);
}

void TestWidthSection::testCurrentWidth_Default()
{
    int defaultWidth = m_section->currentWidth();
    QVERIFY(defaultWidth >= m_section->minWidth());
    QVERIFY(defaultWidth <= m_section->maxWidth());
}

void TestWidthSection::testCurrentWidth_Clamping()
{
    m_section->setWidthRange(5, 15);

    // Set below minimum
    m_section->setCurrentWidth(1);
    QCOMPARE(m_section->currentWidth(), 5);

    // Set above maximum
    m_section->setCurrentWidth(100);
    QCOMPARE(m_section->currentWidth(), 15);
}

// ============================================================================
// Wheel Event Tests
// ============================================================================

void TestWidthSection::testHandleWheel_Increase()
{
    m_section->setWidthRange(1, 20);
    m_section->setCurrentWidth(10);

    bool changed = m_section->handleWheel(120);  // Positive delta = increase

    QVERIFY(changed);
    QVERIFY(m_section->currentWidth() > 10);
}

void TestWidthSection::testHandleWheel_Decrease()
{
    m_section->setWidthRange(1, 20);
    m_section->setCurrentWidth(10);

    bool changed = m_section->handleWheel(-120);  // Negative delta = decrease

    QVERIFY(changed);
    QVERIFY(m_section->currentWidth() < 10);
}

void TestWidthSection::testHandleWheel_AtMax()
{
    m_section->setWidthRange(1, 20);
    m_section->setCurrentWidth(20);

    bool changed = m_section->handleWheel(120);  // Try to increase

    QVERIFY(!changed);  // Already at max
    QCOMPARE(m_section->currentWidth(), 20);
}

void TestWidthSection::testHandleWheel_AtMin()
{
    m_section->setWidthRange(1, 20);
    m_section->setCurrentWidth(1);

    bool changed = m_section->handleWheel(-120);  // Try to decrease

    QVERIFY(!changed);  // Already at min
    QCOMPARE(m_section->currentWidth(), 1);
}

// ============================================================================
// Layout Tests
// ============================================================================

void TestWidthSection::testPreferredWidth()
{
    int width = m_section->preferredWidth();
    QVERIFY(width > 0);
}

void TestWidthSection::testUpdateLayout()
{
    m_section->updateLayout(10, 50, 0);

    QRect rect = m_section->boundingRect();
    QVERIFY(!rect.isEmpty());
}

void TestWidthSection::testBoundingRect()
{
    m_section->updateLayout(0, 50, 10);

    QRect rect = m_section->boundingRect();
    QVERIFY(rect.left() >= 0);
    QVERIFY(rect.width() > 0);
}

// ============================================================================
// Hit Testing Tests
// ============================================================================

void TestWidthSection::testContains_Inside()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    QPoint insidePoint = rect.center();

    QVERIFY(m_section->contains(insidePoint));
}

void TestWidthSection::testContains_Outside()
{
    m_section->updateLayout(0, 50, 0);

    QPoint outsidePoint(-100, -100);

    QVERIFY(!m_section->contains(outsidePoint));
}

// ============================================================================
// Click Handling Tests
// ============================================================================

void TestWidthSection::testHandleClick()
{
    m_section->updateLayout(0, 50, 0);

    const QRect rect = m_section->boundingRect();
    QVERIFY(m_section->handleClick(rect.center()));
    QVERIFY(!m_section->handleClick(QPoint(-100, -100)));
}

// ============================================================================
// Hover Tests
// ============================================================================

void TestWidthSection::testUpdateHovered()
{
    m_section->updateLayout(0, 50, 0);

    const QPoint inside = m_section->boundingRect().center();
    const QPoint outside(-100, -100);

    QVERIFY(m_section->updateHovered(inside));
    QVERIFY(!m_section->updateHovered(inside));
    QVERIFY(m_section->updateHovered(outside));
}

// ============================================================================
// Signal Tests
// ============================================================================

void TestWidthSection::testWidthChangedSignal()
{
    m_section->setWidthRange(1, 20);
    m_section->setCurrentWidth(10);

    QSignalSpy spy(m_section, &WidthSection::widthChanged);

    m_section->handleWheel(120);

    QCOMPARE(spy.count(), 1);

    QList<QVariant> args = spy.takeFirst();
    int newWidth = args.at(0).toInt();
    QVERIFY(newWidth > 10);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestWidthSection::testDraw_Basic()
{
    m_section->setCurrentWidth(10);
    m_section->updateLayout(0, 50, 10);

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);
    m_section->draw(painter, config);

    painter.end();

    // Should have drawn a width preview (dot/circle)
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

void TestWidthSection::testDraw_DifferentWidths()
{
    m_section->setWidthRange(1, 20);
    m_section->updateLayout(0, 50, 10);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);

    // Test drawing with different widths
    QList<int> widths = { 1, 5, 10, 15, 20 };

    for (int width : widths) {
        m_section->setCurrentWidth(width);

        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);

        m_section->draw(painter, config);
        painter.end();

        bool hasColor = false;
        for (int y = 0; y < image.height() && !hasColor; ++y) {
            for (int x = 0; x < image.width() && !hasColor; ++x) {
                if (image.pixelColor(x, y) != Qt::white) {
                    hasColor = true;
                }
            }
        }
        QVERIFY(hasColor);
    }
}

QTEST_MAIN(TestWidthSection)
#include "tst_WidthSection.moc"
