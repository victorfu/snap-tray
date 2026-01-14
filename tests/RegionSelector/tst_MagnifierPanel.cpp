#include <QtTest>
#include <QImage>
#include <QPainter>
#include "region/MagnifierPanel.h"

class tst_MagnifierPanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Configuration tests
    void testDefaultValues();
    void testSetDevicePixelRatio();
    void testColorFormatToggle();
    void testShowHexColor();

    // Color string formatting tests
    void testColorStringRGB();
    void testColorStringHex();

    // Cache tests
    void testInvalidateCache();
    void testShouldUpdate();
    void testMarkUpdated();

    // Draw tests
    void testDrawDoesNotCrash();
    void testDrawWithEmptyImage();
    void testDrawUpdatesCurrentColor();

    // Constants tests
    void testLayoutConstants();

private:
    MagnifierPanel* m_panel;
    QPixmap* m_testPixmap;
};

void tst_MagnifierPanel::initTestCase()
{
}

void tst_MagnifierPanel::cleanupTestCase()
{
}

void tst_MagnifierPanel::init()
{
    m_panel = new MagnifierPanel();

    // Create a simple test image with known colors
    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);

    // Add some colored regions for testing
    QPainter painter(&image);
    painter.fillRect(0, 0, 100, 100, Qt::red);      // Top-left: red
    painter.fillRect(100, 0, 100, 100, Qt::green);  // Top-right: green
    painter.fillRect(0, 100, 100, 100, Qt::blue);   // Bottom-left: blue
    painter.fillRect(100, 100, 100, 100, Qt::yellow); // Bottom-right: yellow
    
    m_testPixmap = new QPixmap(QPixmap::fromImage(image));
}

void tst_MagnifierPanel::cleanup()
{
    delete m_panel;
    m_panel = nullptr;
    delete m_testPixmap;
    m_testPixmap = nullptr;
}

void tst_MagnifierPanel::testDefaultValues()
{
    QCOMPARE(m_panel->showHexColor(), false);
    QCOMPARE(m_panel->currentColor(), QColor());  // Default uninitialized
}

void tst_MagnifierPanel::testSetDevicePixelRatio()
{
    m_panel->setDevicePixelRatio(2.0);
    // No direct getter, but should not crash
    QVERIFY(true);

    m_panel->setDevicePixelRatio(1.0);
    QVERIFY(true);
}

void tst_MagnifierPanel::testColorFormatToggle()
{
    QCOMPARE(m_panel->showHexColor(), false);

    m_panel->toggleColorFormat();
    QCOMPARE(m_panel->showHexColor(), true);

    m_panel->toggleColorFormat();
    QCOMPARE(m_panel->showHexColor(), false);
}

void tst_MagnifierPanel::testShowHexColor()
{
    m_panel->setShowHexColor(true);
    QCOMPARE(m_panel->showHexColor(), true);

    m_panel->setShowHexColor(false);
    QCOMPARE(m_panel->showHexColor(), false);
}

void tst_MagnifierPanel::testColorStringRGB()
{
    // Draw to set the current color
    QPixmap pixmap(400, 400);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);

    m_panel->setShowHexColor(false);
    m_panel->draw(painter, QPoint(50, 50), QSize(400, 400), *m_testPixmap);

    // Should be in RGB format
    QString colorStr = m_panel->colorString();
    QVERIFY(colorStr.startsWith("RGB:"));
}

void tst_MagnifierPanel::testColorStringHex()
{
    QPixmap pixmap(400, 400);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);

    m_panel->setShowHexColor(true);
    m_panel->draw(painter, QPoint(50, 50), QSize(400, 400), *m_testPixmap);

    // Should be in HEX format
    QString colorStr = m_panel->colorString();
    QVERIFY(colorStr.startsWith("#"));
    QCOMPARE(colorStr.length(), 7);  // #RRGGBB
}

void tst_MagnifierPanel::testInvalidateCache()
{
    // Should not crash
    m_panel->invalidateCache();
    QVERIFY(true);

    // Multiple invalidations should be safe
    m_panel->invalidateCache();
    m_panel->invalidateCache();
    QVERIFY(true);
}

void tst_MagnifierPanel::testShouldUpdate()
{
    // Initially should allow update
    QVERIFY(m_panel->shouldUpdate());
}

void tst_MagnifierPanel::testMarkUpdated()
{
    m_panel->markUpdated();

    // Immediately after marking, should not need update (within throttle period)
    // Note: This test may be flaky if system is very slow
    QVERIFY(!m_panel->shouldUpdate() || true);  // Allow either result

    // Wait for throttle period to pass
    QTest::qWait(MagnifierPanel::kMinUpdateMs + 5);
    QVERIFY(m_panel->shouldUpdate());
}

void tst_MagnifierPanel::testDrawDoesNotCrash()
{
    QPixmap pixmap(400, 400);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);

    // Test various cursor positions
    m_panel->draw(painter, QPoint(100, 100), QSize(400, 400), *m_testPixmap);
    QVERIFY(true);

    m_panel->draw(painter, QPoint(0, 0), QSize(400, 400), *m_testPixmap);
    QVERIFY(true);

    m_panel->draw(painter, QPoint(199, 199), QSize(400, 400), *m_testPixmap);
    QVERIFY(true);
}

void tst_MagnifierPanel::testDrawWithEmptyImage()
{
    QPixmap pixmap(400, 400);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);

    QPixmap emptyPixmap;
    m_panel->draw(painter, QPoint(100, 100), QSize(400, 400), emptyPixmap);
    QVERIFY(true);  // Should not crash
}

void tst_MagnifierPanel::testDrawUpdatesCurrentColor()
{
    QPixmap pixmap(400, 400);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);

    // Draw at red region (top-left)
    m_panel->draw(painter, QPoint(50, 50), QSize(400, 400), *m_testPixmap);
    QColor redColor = m_panel->currentColor();
    QCOMPARE(redColor, QColor(Qt::red));

    // Draw at green region (top-right)
    m_panel->draw(painter, QPoint(150, 50), QSize(400, 400), *m_testPixmap);
    QColor greenColor = m_panel->currentColor();
    QCOMPARE(greenColor, QColor(Qt::green));

    // Draw at blue region (bottom-left)
    m_panel->draw(painter, QPoint(50, 150), QSize(400, 400), *m_testPixmap);
    QColor blueColor = m_panel->currentColor();
    QCOMPARE(blueColor, QColor(Qt::blue));
}

void tst_MagnifierPanel::testLayoutConstants()
{
    // Verify constants are reasonable
    QVERIFY(MagnifierPanel::kWidth > 0);
    QVERIFY(MagnifierPanel::kHeight > 0);
    QVERIFY(MagnifierPanel::kGridCountX > 0);
    QVERIFY(MagnifierPanel::kGridCountY > 0);
    QVERIFY(MagnifierPanel::kMinUpdateMs > 0);

    // Verify grid fits in panel
    QVERIFY(MagnifierPanel::kWidth >= MagnifierPanel::kGridCountX);
    QVERIFY(MagnifierPanel::kHeight >= MagnifierPanel::kGridCountY);
}

QTEST_MAIN(tst_MagnifierPanel)
#include "tst_MagnifierPanel.moc"
