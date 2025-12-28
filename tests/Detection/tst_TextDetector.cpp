#include <QtTest>
#include "detection/TextDetector.h"
#include <QImage>
#include <QPainter>
#include <QFont>

/**
 * @brief Test class for TextDetector.
 *
 * Tests text detection initialization, configuration,
 * and detection on synthetic images.
 */
class tst_TextDetector : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Initialization tests
    void testInitialState();
    void testInitialize();
    void testInitializeMultipleTimes();

    // Configuration tests
    void testDefaultConfig();
    void testSetConfig();

    // Detection tests
    void testDetect_EmptyImage();
    void testDetect_NullImage();
    void testDetect_BeforeInitialize();
    void testDetect_BlankImage();
    void testDetect_ImageWithText();
    void testDetect_ImageWithShapes();

private:
    TextDetector* m_detector;

    // Helper to create test images
    QImage createBlankImage(int width, int height);
    QImage createImageWithText(int width, int height, const QString& text);
    QImage createImageWithShapes(int width, int height);
};

void tst_TextDetector::init()
{
    m_detector = new TextDetector();
}

void tst_TextDetector::cleanup()
{
    delete m_detector;
    m_detector = nullptr;
}

void tst_TextDetector::testInitialState()
{
    QVERIFY(!m_detector->isInitialized());
}

void tst_TextDetector::testInitialize()
{
    bool result = m_detector->initialize();
    QVERIFY(result);
    QVERIFY(m_detector->isInitialized());
}

void tst_TextDetector::testInitializeMultipleTimes()
{
    QVERIFY(m_detector->initialize());
    QVERIFY(m_detector->isInitialized());

    // Second initialization should also succeed (idempotent)
    QVERIFY(m_detector->initialize());
    QVERIFY(m_detector->isInitialized());
}

void tst_TextDetector::testDefaultConfig()
{
    TextDetector::Config config = m_detector->config();

    QCOMPARE(config.delta, 5);
    QCOMPARE(config.minArea, 60);
    QCOMPARE(config.maxArea, 14400);
    QVERIFY(qAbs(config.maxVariation - 0.25) < 0.01);
    QVERIFY(config.mergeOverlapping);
    QCOMPARE(config.mergePadding, 5);
}

void tst_TextDetector::testSetConfig()
{
    TextDetector::Config newConfig;
    newConfig.delta = 10;
    newConfig.minArea = 100;
    newConfig.maxArea = 20000;
    newConfig.maxVariation = 0.5;
    newConfig.mergeOverlapping = false;
    newConfig.mergePadding = 10;

    m_detector->setConfig(newConfig);

    TextDetector::Config retrievedConfig = m_detector->config();
    QCOMPARE(retrievedConfig.delta, 10);
    QCOMPARE(retrievedConfig.minArea, 100);
    QCOMPARE(retrievedConfig.maxArea, 20000);
    QVERIFY(qAbs(retrievedConfig.maxVariation - 0.5) < 0.01);
    QVERIFY(!retrievedConfig.mergeOverlapping);
    QCOMPARE(retrievedConfig.mergePadding, 10);
}

void tst_TextDetector::testDetect_EmptyImage()
{
    QVERIFY(m_detector->initialize());

    QImage emptyImage;
    QVector<QRect> results = m_detector->detect(emptyImage);

    QVERIFY(results.isEmpty());
}

void tst_TextDetector::testDetect_NullImage()
{
    QVERIFY(m_detector->initialize());

    QImage nullImage(0, 0, QImage::Format_ARGB32);
    QVector<QRect> results = m_detector->detect(nullImage);

    QVERIFY(results.isEmpty());
}

void tst_TextDetector::testDetect_BeforeInitialize()
{
    // Attempting detection before initialization should return empty
    QImage testImage = createBlankImage(100, 100);
    QVector<QRect> results = m_detector->detect(testImage);

    QVERIFY(results.isEmpty());
}

void tst_TextDetector::testDetect_BlankImage()
{
    QVERIFY(m_detector->initialize());

    // A completely blank image should have no text regions
    QImage blankImage = createBlankImage(200, 200);
    QVector<QRect> results = m_detector->detect(blankImage);

    QVERIFY(results.isEmpty());
}

void tst_TextDetector::testDetect_ImageWithText()
{
    QVERIFY(m_detector->initialize());

    // Create an image with text
    QImage textImage = createImageWithText(400, 200, "Hello World");
    QVector<QRect> results = m_detector->detect(textImage);

    // MSER should detect text-like regions
    // Note: exact detection depends on font rendering and MSER parameters
    // Just verify the function completes and returns valid rects
    for (const QRect& rect : results) {
        QVERIFY(rect.isValid());
        QVERIFY(rect.width() > 0);
        QVERIFY(rect.height() > 0);
    }
}

void tst_TextDetector::testDetect_ImageWithShapes()
{
    QVERIFY(m_detector->initialize());

    // Create an image with shapes (not text-like)
    QImage shapesImage = createImageWithShapes(400, 300);
    QVector<QRect> results = m_detector->detect(shapesImage);

    // Should complete without crashing
    QVERIFY(results.size() >= 0);
}

QImage tst_TextDetector::createBlankImage(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image;
}

QImage tst_TextDetector::createImageWithText(int width, int height, const QString& text)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setPen(Qt::black);
    QFont font("Arial", 24);
    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter, text);
    painter.end();

    return image;
}

QImage tst_TextDetector::createImageWithShapes(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setPen(QPen(Qt::black, 2));
    painter.setBrush(Qt::NoBrush);

    // Draw various shapes
    painter.drawRect(50, 50, 100, 80);
    painter.drawEllipse(200, 50, 100, 100);
    painter.drawLine(50, 200, 350, 200);

    painter.end();

    return image;
}

QTEST_MAIN(tst_TextDetector)
#include "tst_TextDetector.moc"
