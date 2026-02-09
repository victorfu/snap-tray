#include <QtTest>
#include "detection/AutoBlurManager.h"
#include "settings/AutoBlurSettingsManager.h"
#include "settings/Settings.h"
#include <QImage>
#include <QPainter>
#include <QSettings>

/**
 * @brief Test class for AutoBlurManager.
 *
 * Tests the orchestration of face detection,
 * blur application, and settings management.
 */
class tst_AutoBlurManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Initialization tests
    void testInitialState();
    void testInitialize();
    void testInitializeMultipleTimes();

    // Options tests
    void testDefaultOptions();
    void testSetOptions();
    void testOptionsBlurIntensityClamp();
    void testIntensityToBlockSizeMapping();

    // Settings persistence tests
    void testSaveAndLoadSettings();
    void testSettingsDefaults();

    // Detection tests
    void testDetect_EmptyImage();
    void testDetect_NullImage();
    void testDetect_BeforeInitialize();
    void testDetect_ValidImage();
    void testDetect_FacesEnabled();
    void testDetect_FacesDisabled();

    // Blur application tests
    void testApplyBlur_EmptyRegions();
    void testApplyBlur_GaussianBlur();
    void testApplyBlur_PixelateBlur();
    void testApplyBlur_SingleRegion();
    void testApplyBlur_MultipleRegions();

    // DetectAndBlur combined tests
    void testDetectAndBlur_EmptyImage();
    void testDetectAndBlur_ValidImage();

private:
    AutoBlurManager* m_manager;

    // Helper to create test images
    QImage createTestImage(int width, int height);
    QImage createImageWithRegions(int width, int height);

    // Clear test settings
    void clearTestSettings();
};

void tst_AutoBlurManager::initTestCase()
{
    // Clear any existing test settings
    clearTestSettings();
}

void tst_AutoBlurManager::cleanupTestCase()
{
    // Clean up settings after all tests
    clearTestSettings();
}

void tst_AutoBlurManager::init()
{
    m_manager = new AutoBlurManager();
}

void tst_AutoBlurManager::cleanup()
{
    delete m_manager;
    m_manager = nullptr;
}

void tst_AutoBlurManager::clearTestSettings()
{
    QSettings settings = SnapTray::getSettings();
    settings.remove("detection/autoBlurEnabled");
    settings.remove("detection/detectFaces");
    settings.remove("detection/blurIntensity");
    settings.remove("detection/blurType");
    settings.sync();
}

void tst_AutoBlurManager::testInitialState()
{
    QVERIFY(!m_manager->isInitialized());
}

void tst_AutoBlurManager::testInitialize()
{
    bool result = m_manager->initialize();
    QVERIFY(result);
    QVERIFY(m_manager->isInitialized());
}

void tst_AutoBlurManager::testInitializeMultipleTimes()
{
    QVERIFY(m_manager->initialize());
    QVERIFY(m_manager->isInitialized());

    // Second initialization should also succeed
    QVERIFY(m_manager->initialize());
    QVERIFY(m_manager->isInitialized());
}

void tst_AutoBlurManager::testDefaultOptions()
{
    AutoBlurManager::Options opts = m_manager->options();

    QVERIFY(opts.enabled);
    QVERIFY(opts.detectFaces);
    QCOMPARE(opts.blurIntensity, 50);
    QCOMPARE(opts.blurType, AutoBlurManager::BlurType::Pixelate);
}

void tst_AutoBlurManager::testSetOptions()
{
    AutoBlurManager::Options newOpts;
    newOpts.enabled = false;
    newOpts.detectFaces = true;
    newOpts.blurIntensity = 75;
    newOpts.blurType = AutoBlurManager::BlurType::Gaussian;

    m_manager->setOptions(newOpts);

    AutoBlurManager::Options retrieved = m_manager->options();
    QVERIFY(!retrieved.enabled);
    QVERIFY(retrieved.detectFaces);
    QCOMPARE(retrieved.blurIntensity, 75);
    QCOMPARE(retrieved.blurType, AutoBlurManager::BlurType::Gaussian);
}

void tst_AutoBlurManager::testOptionsBlurIntensityClamp()
{
    AutoBlurManager::Options opts;

    // Test that values outside valid range are stored as-is
    // (clamping happens at usage time, not storage time)
    opts.blurIntensity = -10;
    m_manager->setOptions(opts);
    QCOMPARE(m_manager->options().blurIntensity, -10);

    opts.blurIntensity = 200;
    m_manager->setOptions(opts);
    QCOMPARE(m_manager->options().blurIntensity, 200);

    // Test valid value
    opts.blurIntensity = 50;
    m_manager->setOptions(opts);
    QCOMPARE(m_manager->options().blurIntensity, 50);
}

void tst_AutoBlurManager::testIntensityToBlockSizeMapping()
{
    QCOMPARE(AutoBlurManager::intensityToBlockSize(1), 4);
    QCOMPARE(AutoBlurManager::intensityToBlockSize(25), 9);
    QCOMPARE(AutoBlurManager::intensityToBlockSize(50), 14);
    QCOMPARE(AutoBlurManager::intensityToBlockSize(75), 19);
    QCOMPARE(AutoBlurManager::intensityToBlockSize(100), 24);

    // Clamp out-of-range values.
    QCOMPARE(AutoBlurManager::intensityToBlockSize(0), 4);
    QCOMPARE(AutoBlurManager::intensityToBlockSize(101), 24);
}

void tst_AutoBlurManager::testSaveAndLoadSettings()
{
    clearTestSettings();

    AutoBlurSettingsManager::Options opts;
    opts.enabled = true;
    opts.detectFaces = false;
    opts.blurIntensity = 80;
    opts.blurType = AutoBlurSettingsManager::BlurType::Gaussian;

    AutoBlurSettingsManager::instance().save(opts);

    // Load settings from storage
    AutoBlurSettingsManager::Options loaded = AutoBlurSettingsManager::instance().load();
    QVERIFY(loaded.enabled);
    QVERIFY(!loaded.detectFaces);
    QCOMPARE(loaded.blurIntensity, 80);
    QCOMPARE(loaded.blurType, AutoBlurSettingsManager::BlurType::Gaussian);
}

void tst_AutoBlurManager::testSettingsDefaults()
{
    clearTestSettings();

    // Load settings when none exist - should use defaults
    AutoBlurSettingsManager::Options opts = AutoBlurSettingsManager::instance().load();
    QVERIFY(opts.enabled);
    QVERIFY(opts.detectFaces);
    QCOMPARE(opts.blurIntensity, 50);
    QCOMPARE(opts.blurType, AutoBlurSettingsManager::BlurType::Pixelate);
}

void tst_AutoBlurManager::testDetect_EmptyImage()
{
    QVERIFY(m_manager->initialize());

    QImage emptyImage;
    AutoBlurManager::DetectionResult result = m_manager->detect(emptyImage);

    QVERIFY(!result.success);
    QVERIFY(result.faceRegions.isEmpty());
}

void tst_AutoBlurManager::testDetect_NullImage()
{
    QVERIFY(m_manager->initialize());

    QImage nullImage(0, 0, QImage::Format_ARGB32);
    AutoBlurManager::DetectionResult result = m_manager->detect(nullImage);

    QVERIFY(!result.success);
}

void tst_AutoBlurManager::testDetect_BeforeInitialize()
{
    QImage testImage = createTestImage(200, 200);
    AutoBlurManager::DetectionResult result = m_manager->detect(testImage);

    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
}

void tst_AutoBlurManager::testDetect_ValidImage()
{
    QVERIFY(m_manager->initialize());

    QImage testImage = createTestImage(400, 300);
    AutoBlurManager::DetectionResult result = m_manager->detect(testImage);

    QVERIFY(result.success);
    // Results may be empty (no actual faces in synthetic image)
    QVERIFY(result.errorMessage.isEmpty());
}

void tst_AutoBlurManager::testDetect_FacesEnabled()
{
    QVERIFY(m_manager->initialize());

    AutoBlurManager::Options opts = m_manager->options();
    opts.detectFaces = true;
    m_manager->setOptions(opts);

    QImage testImage = createTestImage(400, 300);
    AutoBlurManager::DetectionResult result = m_manager->detect(testImage);

    QVERIFY(result.success);
}

void tst_AutoBlurManager::testDetect_FacesDisabled()
{
    QVERIFY(m_manager->initialize());

    AutoBlurManager::Options opts = m_manager->options();
    opts.detectFaces = false;
    m_manager->setOptions(opts);

    QImage testImage = createTestImage(400, 300);
    AutoBlurManager::DetectionResult result = m_manager->detect(testImage);

    QVERIFY(result.success);
    // Face regions should be empty when detectFaces is false
    QVERIFY(result.faceRegions.isEmpty());
}

void tst_AutoBlurManager::testApplyBlur_EmptyRegions()
{
    QVERIFY(m_manager->initialize());

    QImage testImage = createTestImage(200, 200);
    QImage originalCopy = testImage.copy();

    QVector<QRect> emptyRegions;
    m_manager->applyBlur(testImage, emptyRegions, 50, AutoBlurManager::BlurType::Pixelate);

    // Image should be unchanged
    QCOMPARE(testImage, originalCopy);
}

void tst_AutoBlurManager::testApplyBlur_GaussianBlur()
{
    QVERIFY(m_manager->initialize());

    QImage testImage = createImageWithRegions(200, 200);
    QImage originalCopy = testImage.copy();

    QVector<QRect> regions;
    regions.append(QRect(50, 50, 100, 100));

    m_manager->applyBlur(testImage, regions, 50, AutoBlurManager::BlurType::Gaussian);

    // Image should be modified
    QVERIFY(testImage != originalCopy);
}

void tst_AutoBlurManager::testApplyBlur_PixelateBlur()
{
    QVERIFY(m_manager->initialize());

    QImage testImage = createImageWithRegions(200, 200);
    QImage originalCopy = testImage.copy();

    QVector<QRect> regions;
    regions.append(QRect(50, 50, 100, 100));

    m_manager->applyBlur(testImage, regions, 50, AutoBlurManager::BlurType::Pixelate);

    // Image should be modified
    QVERIFY(testImage != originalCopy);
}

void tst_AutoBlurManager::testApplyBlur_SingleRegion()
{
    QVERIFY(m_manager->initialize());

    QImage testImage = createImageWithRegions(200, 200);

    QVector<QRect> regions;
    regions.append(QRect(50, 50, 50, 50));

    // Should not crash
    m_manager->applyBlur(testImage, regions, 50, AutoBlurManager::BlurType::Pixelate);

    QVERIFY(!testImage.isNull());
}

void tst_AutoBlurManager::testApplyBlur_MultipleRegions()
{
    QVERIFY(m_manager->initialize());

    QImage testImage = createImageWithRegions(400, 300);

    QVector<QRect> regions;
    regions.append(QRect(10, 10, 50, 50));
    regions.append(QRect(100, 100, 80, 60));
    regions.append(QRect(250, 50, 100, 100));

    // Should not crash
    m_manager->applyBlur(testImage, regions, 50, AutoBlurManager::BlurType::Pixelate);

    QVERIFY(!testImage.isNull());
}

void tst_AutoBlurManager::testDetectAndBlur_EmptyImage()
{
    QVERIFY(m_manager->initialize());

    QImage emptyImage;
    AutoBlurManager::DetectionResult result = m_manager->detectAndBlur(emptyImage);

    QVERIFY(!result.success);
}

void tst_AutoBlurManager::testDetectAndBlur_ValidImage()
{
    QVERIFY(m_manager->initialize());

    QImage testImage = createTestImage(400, 300);
    AutoBlurManager::DetectionResult result = m_manager->detectAndBlur(testImage);

    QVERIFY(result.success);
    QVERIFY(!testImage.isNull());
}

QImage tst_AutoBlurManager::createTestImage(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setBrush(Qt::gray);
    painter.drawEllipse(width/4, height/4, width/2, height/2);
    painter.end();

    return image;
}

QImage tst_AutoBlurManager::createImageWithRegions(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);

    // Draw some colorful regions
    painter.setBrush(Qt::red);
    painter.drawRect(10, 10, 50, 50);

    painter.setBrush(Qt::blue);
    painter.drawRect(100, 100, 80, 60);

    painter.setBrush(Qt::green);
    painter.drawEllipse(50, 50, 100, 100);

    painter.end();

    return image;
}

QTEST_MAIN(tst_AutoBlurManager)
#include "tst_AutoBlurManager.moc"
