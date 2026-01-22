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

    // Draw tests
    void testDrawDoesNotCrash();
    void testDrawWithEmptyImage();
    void testDrawUpdatesCurrentColor();

    // Constants tests
    void testLayoutConstants();

    // Pixel sampling tests (critical for high DPI displays)
    void testPixelSamplingAllQuadrants();
    void testPixelSamplingWithHighDPI();
    void testPixelSamplingAtEdges();
    void testPixelSamplingWithRGB32Format();

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

void tst_MagnifierPanel::testPixelSamplingAllQuadrants()
{
    // Create a larger test image with distinct colors in each quadrant
    // This test verifies that pixel sampling works correctly in ALL quadrants,
    // not just the top-left (which was a bug with incorrect coordinate handling)
    QImage image(400, 400, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.fillRect(0, 0, 200, 200, QColor(255, 0, 0));      // Top-left: pure red
    painter.fillRect(200, 0, 200, 200, QColor(0, 255, 0));    // Top-right: pure green
    painter.fillRect(0, 200, 200, 200, QColor(0, 0, 255));    // Bottom-left: pure blue
    painter.fillRect(200, 200, 200, 200, QColor(255, 255, 0)); // Bottom-right: yellow
    painter.end();

    QPixmap testPixmap = QPixmap::fromImage(image);

    QPixmap outputPixmap(600, 600);
    outputPixmap.fill(Qt::transparent);
    QPainter outputPainter(&outputPixmap);

    m_panel->setDevicePixelRatio(1.0);

    // Test top-left quadrant (red)
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(100, 100), QSize(600, 600), testPixmap);
    QColor topLeftColor = m_panel->currentColor();
    QCOMPARE(topLeftColor.red(), 255);
    QCOMPARE(topLeftColor.green(), 0);
    QCOMPARE(topLeftColor.blue(), 0);

    // Test top-right quadrant (green)
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(300, 100), QSize(600, 600), testPixmap);
    QColor topRightColor = m_panel->currentColor();
    QCOMPARE(topRightColor.red(), 0);
    QCOMPARE(topRightColor.green(), 255);
    QCOMPARE(topRightColor.blue(), 0);

    // Test bottom-left quadrant (blue)
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(100, 300), QSize(600, 600), testPixmap);
    QColor bottomLeftColor = m_panel->currentColor();
    QCOMPARE(bottomLeftColor.red(), 0);
    QCOMPARE(bottomLeftColor.green(), 0);
    QCOMPARE(bottomLeftColor.blue(), 255);

    // Test bottom-right quadrant (yellow)
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(300, 300), QSize(600, 600), testPixmap);
    QColor bottomRightColor = m_panel->currentColor();
    QCOMPARE(bottomRightColor.red(), 255);
    QCOMPARE(bottomRightColor.green(), 255);
    QCOMPARE(bottomRightColor.blue(), 0);
}

void tst_MagnifierPanel::testPixelSamplingWithHighDPI()
{
    // Create a high DPI test image (simulating 2x Retina display)
    // Physical pixels: 800x800, Logical pixels: 400x400
    QImage image(800, 800, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.fillRect(0, 0, 400, 400, QColor(255, 0, 0));      // Top-left: red
    painter.fillRect(400, 0, 400, 400, QColor(0, 255, 0));    // Top-right: green
    painter.fillRect(0, 400, 400, 400, QColor(0, 0, 255));    // Bottom-left: blue
    painter.fillRect(400, 400, 400, 400, QColor(255, 255, 0)); // Bottom-right: yellow
    painter.end();

    QPixmap testPixmap = QPixmap::fromImage(image);
    testPixmap.setDevicePixelRatio(2.0);  // Set as 2x DPI

    QPixmap outputPixmap(600, 600);
    outputPixmap.fill(Qt::transparent);
    QPainter outputPainter(&outputPixmap);

    m_panel->setDevicePixelRatio(2.0);

    // Test top-left quadrant (red) - using logical coordinates
    // Logical (100, 100) -> Physical (200, 200) which is in top-left red area
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(100, 100), QSize(600, 600), testPixmap);
    QColor topLeftColor = m_panel->currentColor();
    QCOMPARE(topLeftColor.red(), 255);
    QCOMPARE(topLeftColor.green(), 0);
    QCOMPARE(topLeftColor.blue(), 0);

    // Test top-right quadrant (green)
    // Logical (300, 100) -> Physical (600, 200) which is in top-right green area
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(300, 100), QSize(600, 600), testPixmap);
    QColor topRightColor = m_panel->currentColor();
    QCOMPARE(topRightColor.red(), 0);
    QCOMPARE(topRightColor.green(), 255);
    QCOMPARE(topRightColor.blue(), 0);

    // Test bottom-left quadrant (blue)
    // Logical (100, 300) -> Physical (200, 600) which is in bottom-left blue area
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(100, 300), QSize(600, 600), testPixmap);
    QColor bottomLeftColor = m_panel->currentColor();
    QCOMPARE(bottomLeftColor.red(), 0);
    QCOMPARE(bottomLeftColor.green(), 0);
    QCOMPARE(bottomLeftColor.blue(), 255);

    // Test bottom-right quadrant (yellow)
    // Logical (300, 300) -> Physical (600, 600) which is in bottom-right yellow area
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(300, 300), QSize(600, 600), testPixmap);
    QColor bottomRightColor = m_panel->currentColor();
    QCOMPARE(bottomRightColor.red(), 255);
    QCOMPARE(bottomRightColor.green(), 255);
    QCOMPARE(bottomRightColor.blue(), 0);
}

void tst_MagnifierPanel::testPixelSamplingAtEdges()
{
    // Test sampling near image boundaries to ensure no crashes or invalid reads
    QImage image(200, 200, QImage::Format_RGB32);
    image.fill(QColor(128, 64, 32));  // Fill with known color

    QPixmap testPixmap = QPixmap::fromImage(image);

    QPixmap outputPixmap(400, 400);
    outputPixmap.fill(Qt::transparent);
    QPainter outputPainter(&outputPixmap);

    m_panel->setDevicePixelRatio(1.0);

    // Test at origin (0, 0) - partial magnifier visible
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(0, 0), QSize(400, 400), testPixmap);
    QVERIFY(true);  // Should not crash

    // Test at top-right corner
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(199, 0), QSize(400, 400), testPixmap);
    QVERIFY(true);

    // Test at bottom-left corner
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(0, 199), QSize(400, 400), testPixmap);
    QVERIFY(true);

    // Test at bottom-right corner
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(199, 199), QSize(400, 400), testPixmap);
    QVERIFY(true);

    // Test beyond image bounds (should handle gracefully)
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(250, 250), QSize(400, 400), testPixmap);
    QVERIFY(true);
}

void tst_MagnifierPanel::testPixelSamplingWithRGB32Format()
{
    // Test with RGB32 format specifically (the format returned by QScreen::grabWindow on macOS)
    // This ensures the direct memcpy approach works correctly with this format
    QImage image(400, 400, QImage::Format_RGB32);

    // Fill with a gradient pattern to test accurate sampling
    for (int y = 0; y < 400; ++y) {
        for (int x = 0; x < 400; ++x) {
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
        }
    }

    QPixmap testPixmap = QPixmap::fromImage(image);

    QPixmap outputPixmap(600, 600);
    outputPixmap.fill(Qt::transparent);
    QPainter outputPainter(&outputPixmap);

    m_panel->setDevicePixelRatio(1.0);

    // Test sampling at specific coordinate
    int testX = 150;
    int testY = 200;
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(testX, testY), QSize(600, 600), testPixmap);

    QColor sampledColor = m_panel->currentColor();
    QColor expectedColor(testX % 256, testY % 256, (testX + testY) % 256);

    QCOMPARE(sampledColor.red(), expectedColor.red());
    QCOMPARE(sampledColor.green(), expectedColor.green());
    QCOMPARE(sampledColor.blue(), expectedColor.blue());

    // Test another coordinate
    testX = 50;
    testY = 100;
    m_panel->invalidateCache();
    m_panel->draw(outputPainter, QPoint(testX, testY), QSize(600, 600), testPixmap);

    sampledColor = m_panel->currentColor();
    expectedColor = QColor(testX % 256, testY % 256, (testX + testY) % 256);

    QCOMPARE(sampledColor.red(), expectedColor.red());
    QCOMPARE(sampledColor.green(), expectedColor.green());
    QCOMPARE(sampledColor.blue(), expectedColor.blue());
}

QTEST_MAIN(tst_MagnifierPanel)
#include "tst_MagnifierPanel.moc"
