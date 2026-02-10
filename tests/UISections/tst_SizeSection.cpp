#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include <QSignalSpy>
#include "ui/sections/SizeSection.h"
#include "ToolbarStyle.h"

/**
 * @brief Tests for SizeSection UI component (Step Badge sizes)
 *
 * Covers:
 * - Size management (Small/Medium/Large)
 * - Layout calculations
 * - Hit testing and button detection
 * - Click handling
 * - Hover state
 * - Signal emission
 * - Drawing
 */
class TestSizeSection : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Size tests
    void testSetSize_Small();
    void testSetSize_Medium();
    void testSetSize_Large();
    void testSize_Default();

    // Layout tests
    void testPreferredWidth();
    void testUpdateLayout();
    void testBoundingRect();

    // Hit testing tests
    void testContains_Inside();
    void testContains_Outside();

    // Click handling tests
    void testHandleClick_SmallButton();
    void testHandleClick_MediumButton();
    void testHandleClick_LargeButton();
    void testHandleClick_Outside();

    // Hover tests
    void testUpdateHovered_OnButton();
    void testUpdateHovered_OffButton();

    // Signal tests
    void testSizeChangedSignal();

    // Drawing tests
    void testDraw_Basic();
    void testDraw_AllSizes();

private:
    SizeSection* m_section = nullptr;
};

void TestSizeSection::init()
{
    m_section = new SizeSection();
}

void TestSizeSection::cleanup()
{
    delete m_section;
    m_section = nullptr;
}

// ============================================================================
// Size Tests
// ============================================================================

void TestSizeSection::testSetSize_Small()
{
    m_section->setSize(StepBadgeSize::Small);
    QCOMPARE(m_section->size(), StepBadgeSize::Small);
}

void TestSizeSection::testSetSize_Medium()
{
    m_section->setSize(StepBadgeSize::Medium);
    QCOMPARE(m_section->size(), StepBadgeSize::Medium);
}

void TestSizeSection::testSetSize_Large()
{
    m_section->setSize(StepBadgeSize::Large);
    QCOMPARE(m_section->size(), StepBadgeSize::Large);
}

void TestSizeSection::testSize_Default()
{
    QCOMPARE(m_section->size(), StepBadgeSize::Medium);
}

// ============================================================================
// Layout Tests
// ============================================================================

void TestSizeSection::testPreferredWidth()
{
    int width = m_section->preferredWidth();
    QVERIFY(width > 0);
}

void TestSizeSection::testUpdateLayout()
{
    m_section->updateLayout(10, 50, 0);

    QRect rect = m_section->boundingRect();
    QVERIFY(!rect.isEmpty());
}

void TestSizeSection::testBoundingRect()
{
    m_section->updateLayout(0, 50, 10);

    QRect rect = m_section->boundingRect();
    QVERIFY(rect.left() >= 0);
    QVERIFY(rect.width() > 0);
}

// ============================================================================
// Hit Testing Tests
// ============================================================================

void TestSizeSection::testContains_Inside()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    QPoint insidePoint = rect.center();

    QVERIFY(m_section->contains(insidePoint));
}

void TestSizeSection::testContains_Outside()
{
    m_section->updateLayout(0, 50, 0);

    QPoint outsidePoint(-100, -100);

    QVERIFY(!m_section->contains(outsidePoint));
}

// ============================================================================
// Click Handling Tests
// ============================================================================

void TestSizeSection::testHandleClick_SmallButton()
{
    m_section->setSize(StepBadgeSize::Large);  // Start with large
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &SizeSection::sizeChanged);

    QRect rect = m_section->boundingRect();
    // First button (Small)
    QPoint smallButtonPos(rect.left() + 11, rect.center().y());

    QVERIFY(m_section->handleClick(smallButtonPos));
    QCOMPARE(m_section->size(), StepBadgeSize::Small);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<StepBadgeSize>(), StepBadgeSize::Small);
}

void TestSizeSection::testHandleClick_MediumButton()
{
    m_section->setSize(StepBadgeSize::Small);  // Start with small
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &SizeSection::sizeChanged);

    QRect rect = m_section->boundingRect();
    // Second button (Medium)
    QPoint mediumButtonPos(rect.center().x(), rect.center().y());

    QVERIFY(m_section->handleClick(mediumButtonPos));
    QCOMPARE(m_section->size(), StepBadgeSize::Medium);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<StepBadgeSize>(), StepBadgeSize::Medium);
}

void TestSizeSection::testHandleClick_LargeButton()
{
    m_section->setSize(StepBadgeSize::Small);  // Start with small
    m_section->updateLayout(0, 50, 0);

    QSignalSpy spy(m_section, &SizeSection::sizeChanged);

    QRect rect = m_section->boundingRect();
    // Third button (Large)
    QPoint largeButtonPos(rect.right() - 11, rect.center().y());

    QVERIFY(m_section->handleClick(largeButtonPos));
    QCOMPARE(m_section->size(), StepBadgeSize::Large);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<StepBadgeSize>(), StepBadgeSize::Large);
}

void TestSizeSection::testHandleClick_Outside()
{
    m_section->updateLayout(0, 50, 0);

    bool handled = m_section->handleClick(QPoint(-100, -100));

    QVERIFY(!handled);
}

// ============================================================================
// Hover Tests
// ============================================================================

void TestSizeSection::testUpdateHovered_OnButton()
{
    m_section->updateLayout(0, 50, 0);

    const QPoint inside = m_section->boundingRect().center();
    QVERIFY(m_section->updateHovered(inside));
    QVERIFY(!m_section->updateHovered(inside));
}

void TestSizeSection::testUpdateHovered_OffButton()
{
    m_section->updateLayout(0, 50, 0);

    const QPoint inside = m_section->boundingRect().center();
    const QPoint outside(-100, -100);
    QVERIFY(m_section->updateHovered(inside));
    QVERIFY(m_section->updateHovered(outside));
}

// ============================================================================
// Signal Tests
// ============================================================================

void TestSizeSection::testSizeChangedSignal()
{
    QSignalSpy spy(m_section, &SizeSection::sizeChanged);
    m_section->setSize(StepBadgeSize::Large);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<StepBadgeSize>(), StepBadgeSize::Large);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestSizeSection::testDraw_Basic()
{
    m_section->setSize(StepBadgeSize::Medium);
    m_section->updateLayout(0, 50, 10);

    QImage image(150, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);
    m_section->draw(painter, config);

    painter.end();

    // Should have drawn button icons (circles)
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

void TestSizeSection::testDraw_AllSizes()
{
    m_section->updateLayout(0, 50, 10);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);

    QList<StepBadgeSize> sizes = {
        StepBadgeSize::Small,
        StepBadgeSize::Medium,
        StepBadgeSize::Large
    };

    for (StepBadgeSize size : sizes) {
        m_section->setSize(size);

        QImage image(150, 100, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);

        m_section->draw(painter, config);

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

QTEST_MAIN(TestSizeSection)
#include "tst_SizeSection.moc"
