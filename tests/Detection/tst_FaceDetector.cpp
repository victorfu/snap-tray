#include <QtTest>
#include "detection/FaceDetector.h"
#include <QImage>
#include <QPainter>

/**
 * @brief Test class for FaceDetector.
 *
 * Tests face detection initialization, configuration,
 * and detection on synthetic images.
 */
class tst_FaceDetector : public QObject
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
    void testDetect_SmallImage();
    void testDetect_LargeImage();

private:
    FaceDetector* m_detector;

    // Helper to create a simple test image
    QImage createTestImage(int width, int height);
};

void tst_FaceDetector::init()
{
    m_detector = new FaceDetector();
}

void tst_FaceDetector::cleanup()
{
    delete m_detector;
    m_detector = nullptr;
}

void tst_FaceDetector::testInitialState()
{
    QVERIFY(!m_detector->isInitialized());
}

void tst_FaceDetector::testInitialize()
{
    bool result = m_detector->initialize();
    QVERIFY(result);
    QVERIFY(m_detector->isInitialized());
}

void tst_FaceDetector::testInitializeMultipleTimes()
{
    QVERIFY(m_detector->initialize());
    QVERIFY(m_detector->isInitialized());

    // Second initialization should also succeed (idempotent)
    QVERIFY(m_detector->initialize());
    QVERIFY(m_detector->isInitialized());
}

void tst_FaceDetector::testDefaultConfig()
{
    FaceDetector::Config config = m_detector->config();

    QVERIFY(qAbs(config.scaleFactor - 1.1) < 0.01);
    QCOMPARE(config.minNeighbors, 5);
    QCOMPARE(config.minFaceSize, 30);
    QCOMPARE(config.maxFaceSize, 0);  // 0 means no limit
}

void tst_FaceDetector::testSetConfig()
{
    FaceDetector::Config newConfig;
    newConfig.scaleFactor = 1.2;
    newConfig.minNeighbors = 3;
    newConfig.minFaceSize = 50;
    newConfig.maxFaceSize = 300;

    m_detector->setConfig(newConfig);

    FaceDetector::Config retrievedConfig = m_detector->config();
    QVERIFY(qAbs(retrievedConfig.scaleFactor - 1.2) < 0.01);
    QCOMPARE(retrievedConfig.minNeighbors, 3);
    QCOMPARE(retrievedConfig.minFaceSize, 50);
    QCOMPARE(retrievedConfig.maxFaceSize, 300);
}

void tst_FaceDetector::testDetect_EmptyImage()
{
    QVERIFY(m_detector->initialize());

    QImage emptyImage;
    QVector<QRect> results = m_detector->detect(emptyImage);

    QVERIFY(results.isEmpty());
}

void tst_FaceDetector::testDetect_NullImage()
{
    QVERIFY(m_detector->initialize());

    QImage nullImage(0, 0, QImage::Format_ARGB32);
    QVector<QRect> results = m_detector->detect(nullImage);

    QVERIFY(results.isEmpty());
}

void tst_FaceDetector::testDetect_BeforeInitialize()
{
    // Attempting detection before initialization should return empty
    QImage testImage = createTestImage(100, 100);
    QVector<QRect> results = m_detector->detect(testImage);

    QVERIFY(results.isEmpty());
}

void tst_FaceDetector::testDetect_SmallImage()
{
    QVERIFY(m_detector->initialize());

    // Create a very small image (too small for face detection)
    QImage smallImage = createTestImage(10, 10);
    QVector<QRect> results = m_detector->detect(smallImage);

    // Should return empty (no faces in synthetic image)
    QVERIFY(results.isEmpty());
}

void tst_FaceDetector::testDetect_LargeImage()
{
    QVERIFY(m_detector->initialize());

    // Create a larger test image
    QImage largeImage = createTestImage(640, 480);
    QVector<QRect> results = m_detector->detect(largeImage);

    // Should not crash; results may be empty (no actual faces)
    // Just verify the function completes successfully
    QVERIFY(results.size() >= 0);
}

QImage tst_FaceDetector::createTestImage(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);

    // Draw some shapes to create a non-uniform image
    QPainter painter(&image);
    painter.setBrush(Qt::gray);
    painter.drawEllipse(width/4, height/4, width/2, height/2);
    painter.end();

    return image;
}

QTEST_MAIN(tst_FaceDetector)
#include "tst_FaceDetector.moc"
