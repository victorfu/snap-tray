#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "encoding/EncoderFactory.h"
#include "IVideoEncoder.h"
#include "encoding/NativeGifEncoder.h"

/**
 * @brief Tests for EncoderFactory
 *
 * Covers:
 * - Encoder creation based on format and priority
 * - Configuration validation
 * - Error handling for invalid configurations
 * - Platform-specific encoder availability
 */
class TestEncoderFactory : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Config tests
    void testDefaultConfig();

    // Factory creation tests - GIF
    void testCreateGifEncoder();
    void testCreateGifEncoderWithCustomBitDepth();
    void testCreateGifEncoderStartsEncoder();

    // Factory creation tests - Native
    void testCreateNativeEncoder();
    void testCreateNativeEncoderWithAudio();
    void testCreateNativeEncoderWithQuality();

    // Error handling tests
    void testCreateWithInvalidPath();
    void testCreateWithZeroFrameSize();

private:
    QTemporaryDir* m_tempDir = nullptr;

    EncoderFactory::EncoderConfig createTestConfig(EncoderFactory::Format format);
};

void TestEncoderFactory::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
}

void TestEncoderFactory::cleanupTestCase()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

void TestEncoderFactory::init()
{
}

void TestEncoderFactory::cleanup()
{
}

EncoderFactory::EncoderConfig TestEncoderFactory::createTestConfig(EncoderFactory::Format format)
{
    EncoderFactory::EncoderConfig config;
    config.format = format;
    config.priority = EncoderFactory::Priority::NativeFirst;
    config.frameSize = QSize(640, 480);
    config.frameRate = 30;
    config.quality = 55;
    config.gifBitDepth = 16;

    if (format == EncoderFactory::Format::GIF) {
        config.outputPath = m_tempDir->filePath("test.gif");
    } else {
        config.outputPath = m_tempDir->filePath("test.mp4");
    }

    return config;
}

// ============================================================================
// Config Tests
// ============================================================================

void TestEncoderFactory::testDefaultConfig()
{
    EncoderFactory::EncoderConfig config;

    QCOMPARE(config.format, EncoderFactory::Format::MP4);
    QCOMPARE(config.priority, EncoderFactory::Priority::NativeFirst);
    QCOMPARE(config.frameRate, 30);
    QCOMPARE(config.quality, 55);
    QCOMPARE(config.gifBitDepth, 16);
    QVERIFY(!config.enableAudio);
    QCOMPARE(config.audioSampleRate, 48000);
    QCOMPARE(config.audioChannels, 2);
    QCOMPARE(config.audioBitsPerSample, 16);
}

// ============================================================================
// Factory Creation Tests - GIF
// ============================================================================

void TestEncoderFactory::testCreateGifEncoder()
{
    auto config = createTestConfig(EncoderFactory::Format::GIF);

    auto result = EncoderFactory::create(config, this);

    QVERIFY(result.success);
    QVERIFY(result.hasEncoder());
    QVERIFY(result.gifEncoder != nullptr);
    QVERIFY(result.nativeEncoder == nullptr);
    QVERIFY(!result.isNative);
    QVERIFY(result.errorMessage.isEmpty());

    // Cleanup
    if (result.gifEncoder) {
        result.gifEncoder->abort();
        delete result.gifEncoder;
    }
}

void TestEncoderFactory::testCreateGifEncoderWithCustomBitDepth()
{
    auto config = createTestConfig(EncoderFactory::Format::GIF);
    config.gifBitDepth = 8;

    auto result = EncoderFactory::create(config, this);

    QVERIFY(result.success);
    QVERIFY(result.gifEncoder != nullptr);

    // Verify bit depth was set
    QCOMPARE(result.gifEncoder->maxBitDepth(), 8);

    // Cleanup
    if (result.gifEncoder) {
        result.gifEncoder->abort();
        delete result.gifEncoder;
    }
}

void TestEncoderFactory::testCreateGifEncoderStartsEncoder()
{
    auto config = createTestConfig(EncoderFactory::Format::GIF);

    auto result = EncoderFactory::create(config, this);

    QVERIFY(result.success);
    QVERIFY(result.gifEncoder != nullptr);
    // Factory should start the encoder
    QVERIFY(result.gifEncoder->isRunning());

    // Cleanup
    if (result.gifEncoder) {
        result.gifEncoder->abort();
        delete result.gifEncoder;
    }
}

// ============================================================================
// Factory Creation Tests - Native
// ============================================================================

void TestEncoderFactory::testCreateNativeEncoder()
{
    auto config = createTestConfig(EncoderFactory::Format::MP4);

    auto result = EncoderFactory::create(config, this);

    // Native encoder availability depends on platform
    if (EncoderFactory::isNativeEncoderAvailable()) {
        QVERIFY(result.success);
        QVERIFY(result.hasEncoder());
        QVERIFY(result.nativeEncoder != nullptr);
        QVERIFY(result.isNative);

        // Cleanup
        if (result.nativeEncoder) {
            result.nativeEncoder->abort();
            delete result.nativeEncoder;
        }
    } else {
        // On platforms without native encoder, should fail gracefully
        QVERIFY(!result.success || !result.hasEncoder());
    }
}

void TestEncoderFactory::testCreateNativeEncoderWithAudio()
{
    auto config = createTestConfig(EncoderFactory::Format::MP4);
    config.enableAudio = true;
    config.audioSampleRate = 48000;
    config.audioChannels = 2;
    config.audioBitsPerSample = 16;

    auto result = EncoderFactory::create(config, this);

    if (EncoderFactory::isNativeEncoderAvailable()) {
        QVERIFY(result.success);
        QVERIFY(result.nativeEncoder != nullptr);

        // Cleanup
        if (result.nativeEncoder) {
            result.nativeEncoder->abort();
            delete result.nativeEncoder;
        }
    }
}

void TestEncoderFactory::testCreateNativeEncoderWithQuality()
{
    auto config = createTestConfig(EncoderFactory::Format::MP4);
    config.quality = 80;

    auto result = EncoderFactory::create(config, this);

    if (EncoderFactory::isNativeEncoderAvailable()) {
        QVERIFY(result.success);
        QVERIFY(result.nativeEncoder != nullptr);

        // Cleanup
        if (result.nativeEncoder) {
            result.nativeEncoder->abort();
            delete result.nativeEncoder;
        }
    }
}

// ============================================================================
// Error Handling Tests
// ============================================================================

void TestEncoderFactory::testCreateWithInvalidPath()
{
    auto config = createTestConfig(EncoderFactory::Format::GIF);
    config.outputPath = "/nonexistent/directory/test.gif";

    auto result = EncoderFactory::create(config, this);

    // GIF encoder might still start (it validates path on finish())
    // This test documents actual behavior
    if (result.success && result.gifEncoder) {
        // Encoder started but will fail on finish
        result.gifEncoder->abort();
        delete result.gifEncoder;
    }
}

void TestEncoderFactory::testCreateWithZeroFrameSize()
{
    auto config = createTestConfig(EncoderFactory::Format::GIF);
    config.frameSize = QSize(0, 0);

    auto result = EncoderFactory::create(config, this);

    // Behavior with zero frame size
    if (result.gifEncoder) {
        result.gifEncoder->abort();
        delete result.gifEncoder;
    }
    if (result.nativeEncoder) {
        result.nativeEncoder->abort();
        delete result.nativeEncoder;
    }
}

QTEST_MAIN(TestEncoderFactory)
#include "tst_EncoderFactory.moc"
