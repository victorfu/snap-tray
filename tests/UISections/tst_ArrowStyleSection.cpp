#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include <QSignalSpy>
#include "ui/sections/ArrowStyleSection.h"
#include "ToolbarStyle.h"

/**
 * @brief Tests for ArrowStyleSection UI component
 *
 * Covers:
 * - Arrow style management
 * - Dropdown state
 * - Layout calculations
 * - Hit testing
 * - Click handling (button and dropdown)
 * - Hover state
 * - Signal emission
 * - Drawing
 */
class TestArrowStyleSection : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Arrow style tests
    void testSetArrowStyle();
    void testArrowStyle_Default();
    void testArrowStyle_AllStyles();

    // Dropdown state tests
    void testIsDropdownOpen_Default();
    void testSetDropdownExpandsUpward();

    // Layout tests
    void testPreferredWidth();
    void testUpdateLayout();
    void testBoundingRect();
    void testDropdownRect();

    // Hit testing tests
    void testContains_Inside();
    void testContains_Outside();

    // Click handling tests
    void testHandleClick_Button();
    void testHandleClick_DropdownOption();
    void testHandleClick_Outside();

    // Hover tests
    void testUpdateHovered_OnButton();
    void testUpdateHovered_OnDropdown();
    void testUpdateHovered_Outside();

    // Signal tests
    void testArrowStyleChangedSignal();

    // Drawing tests
    void testDraw_Closed();
    void testDraw_AllStyles();

private:
    ArrowStyleSection* m_section = nullptr;
};

void TestArrowStyleSection::init()
{
    m_section = new ArrowStyleSection();
}

void TestArrowStyleSection::cleanup()
{
    delete m_section;
    m_section = nullptr;
}

// ============================================================================
// Arrow Style Tests
// ============================================================================

void TestArrowStyleSection::testSetArrowStyle()
{
    m_section->setArrowStyle(LineEndStyle::BothArrow);
    QCOMPARE(m_section->arrowStyle(), LineEndStyle::BothArrow);
}

void TestArrowStyleSection::testArrowStyle_Default()
{
    QCOMPARE(m_section->arrowStyle(), LineEndStyle::EndArrow);
}

void TestArrowStyleSection::testArrowStyle_AllStyles()
{
    QList<LineEndStyle> styles = {
        LineEndStyle::None,
        LineEndStyle::EndArrow,
        LineEndStyle::EndArrowOutline,
        LineEndStyle::EndArrowLine,
        LineEndStyle::BothArrow,
        LineEndStyle::BothArrowOutline
    };

    for (LineEndStyle style : styles) {
        m_section->setArrowStyle(style);
        QCOMPARE(m_section->arrowStyle(), style);
    }
}

// ============================================================================
// Dropdown State Tests
// ============================================================================

void TestArrowStyleSection::testIsDropdownOpen_Default()
{
    QVERIFY(!m_section->isDropdownOpen());
}

void TestArrowStyleSection::testSetDropdownExpandsUpward()
{
    m_section->updateLayout(0, 100, 0);
    QPoint buttonPos = m_section->boundingRect().center();

    // Default: dropdown opens downward.
    QVERIFY(m_section->handleClick(buttonPos));
    QVERIFY(m_section->isDropdownOpen());
    QRect downwardRect = m_section->dropdownRect();
    QVERIFY(downwardRect.top() > buttonPos.y());

    // Close and switch to upward expansion.
    QVERIFY(m_section->handleClick(buttonPos));
    QVERIFY(!m_section->isDropdownOpen());

    m_section->setDropdownExpandsUpward(true);
    m_section->updateLayout(0, 100, 0);
    buttonPos = m_section->boundingRect().center();

    QVERIFY(m_section->handleClick(buttonPos));
    QVERIFY(m_section->isDropdownOpen());
    QRect upwardRect = m_section->dropdownRect();
    QVERIFY(upwardRect.bottom() < buttonPos.y());
}

// ============================================================================
// Layout Tests
// ============================================================================

void TestArrowStyleSection::testPreferredWidth()
{
    int width = m_section->preferredWidth();
    QVERIFY(width > 0);
}

void TestArrowStyleSection::testUpdateLayout()
{
    m_section->updateLayout(10, 50, 0);

    QRect rect = m_section->boundingRect();
    QVERIFY(!rect.isEmpty());
}

void TestArrowStyleSection::testBoundingRect()
{
    m_section->updateLayout(0, 50, 10);

    QRect rect = m_section->boundingRect();
    QVERIFY(rect.left() >= 0);
    QVERIFY(rect.width() > 0);
}

void TestArrowStyleSection::testDropdownRect()
{
    m_section->updateLayout(0, 50, 10);

    // Open the dropdown first by clicking
    QRect buttonRect = m_section->boundingRect();
    m_section->handleClick(buttonRect.center());

    if (m_section->isDropdownOpen()) {
        QRect dropdownRect = m_section->dropdownRect();
        QVERIFY(dropdownRect.height() > 0);
    }
}

// ============================================================================
// Hit Testing Tests
// ============================================================================

void TestArrowStyleSection::testContains_Inside()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    QPoint insidePoint = rect.center();

    QVERIFY(m_section->contains(insidePoint));
}

void TestArrowStyleSection::testContains_Outside()
{
    m_section->updateLayout(0, 50, 0);

    QPoint outsidePoint(-100, -100);

    QVERIFY(!m_section->contains(outsidePoint));
}

// ============================================================================
// Click Handling Tests
// ============================================================================

void TestArrowStyleSection::testHandleClick_Button()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    bool handled = m_section->handleClick(rect.center());

    QVERIFY(handled);
    QVERIFY(m_section->isDropdownOpen());
}

void TestArrowStyleSection::testHandleClick_DropdownOption()
{
    m_section->updateLayout(0, 100, 0);

    // First open the dropdown
    QRect buttonRect = m_section->boundingRect();
    QVERIFY(m_section->handleClick(buttonRect.center()));
    QVERIFY(m_section->isDropdownOpen());

    QSignalSpy spy(m_section, &ArrowStyleSection::arrowStyleChanged);

    // Click on the first dropdown option (LineEndStyle::None).
    QRect dropdownRect = m_section->dropdownRect();
    QVERIFY(!dropdownRect.isEmpty());
    QPoint firstOptionPos(dropdownRect.left() + dropdownRect.width() / 2, dropdownRect.top() + 14);
    QVERIFY(m_section->handleClick(firstOptionPos));

    QVERIFY(!m_section->isDropdownOpen());
    QCOMPARE(m_section->arrowStyle(), LineEndStyle::None);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<LineEndStyle>(), LineEndStyle::None);
}

void TestArrowStyleSection::testHandleClick_Outside()
{
    m_section->updateLayout(0, 50, 0);

    bool handled = m_section->handleClick(QPoint(-100, -100));

    QVERIFY(!handled);
}

// ============================================================================
// Hover Tests
// ============================================================================

void TestArrowStyleSection::testUpdateHovered_OnButton()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    bool updated = m_section->updateHovered(rect.center());
    QVERIFY(updated);
    QVERIFY(!m_section->updateHovered(rect.center()));
}

void TestArrowStyleSection::testUpdateHovered_OnDropdown()
{
    m_section->updateLayout(0, 100, 0);

    // Open dropdown
    QRect buttonRect = m_section->boundingRect();
    m_section->handleClick(buttonRect.center());

    if (m_section->isDropdownOpen()) {
        QRect dropdownRect = m_section->dropdownRect();
        QVERIFY(!dropdownRect.isEmpty());
        QVERIFY(m_section->updateHovered(dropdownRect.center()));
        QVERIFY(!m_section->updateHovered(dropdownRect.center()));
    }
}

void TestArrowStyleSection::testUpdateHovered_Outside()
{
    m_section->updateLayout(0, 50, 0);

    QRect rect = m_section->boundingRect();
    QVERIFY(m_section->updateHovered(rect.center()));

    bool updated = m_section->updateHovered(QPoint(-100, -100));
    QVERIFY(updated);
    QVERIFY(!m_section->updateHovered(QPoint(-100, -100)));
}

// ============================================================================
// Signal Tests
// ============================================================================

void TestArrowStyleSection::testArrowStyleChangedSignal()
{
    QSignalSpy spy(m_section, &ArrowStyleSection::arrowStyleChanged);
    m_section->setArrowStyle(LineEndStyle::BothArrow);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<LineEndStyle>(), LineEndStyle::BothArrow);
    m_section->setArrowStyle(LineEndStyle::BothArrow);
    QCOMPARE(spy.count(), 0);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestArrowStyleSection::testDraw_Closed()
{
    m_section->setArrowStyle(LineEndStyle::EndArrow);
    m_section->updateLayout(0, 50, 10);

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);
    m_section->draw(painter, config);

    painter.end();

    // Should have drawn the button
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

void TestArrowStyleSection::testDraw_AllStyles()
{
    m_section->updateLayout(0, 50, 10);

    const ToolbarStyleConfig& config = ToolbarStyleConfig::getStyle(ToolbarStyleType::Light);

    QList<LineEndStyle> styles = {
        LineEndStyle::None,
        LineEndStyle::EndArrow,
        LineEndStyle::EndArrowOutline,
        LineEndStyle::EndArrowLine,
        LineEndStyle::BothArrow,
        LineEndStyle::BothArrowOutline
    };

    for (LineEndStyle style : styles) {
        m_section->setArrowStyle(style);

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

QTEST_MAIN(TestArrowStyleSection)
#include "tst_ArrowStyleSection.moc"
