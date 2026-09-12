#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "encoding/EncoderFactory.h"
#include "IVideoEncoder.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "FakeAudioEncoder.h"

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
    void testActualAudioCapability_data();
    void testActualAudioCapability();
    void testAnimatedFormatsDoNotReportAudioFallback();

    // Factory creation tests - GIF
    void testCreateGifEncoder();

    // Factory creation tests - Native
    void testCreateNativeEncoder();
    void testCreateNativeEncoderWithAudio();
    void testCreateNativeEncoderWithQuality();

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

void TestEncoderFactory::testActualAudioCapability_data()
{
    QTest::addColumn<bool>("requested");
    QTest::addColumn<bool>("supported");
    QTest::addColumn<bool>("configured");
    QTest::addColumn<bool>("started");
    QTest::newRow("audio-ready") << true << true << true << true;
    QTest::newRow("audio-setup-failed") << true << true << false << true;
    QTest::newRow("audio-unsupported") << true << false << true << true;
    QTest::newRow("audio-not-requested") << false << true << true << true;
    QTest::newRow("encoder-failed") << true << true << true << false;
}

void TestEncoderFactory::testActualAudioCapability()
{
    QFETCH(bool, requested);
    QFETCH(bool, supported);
    QFETCH(bool, configured);
    QFETCH(bool, started);
    auto state = std::make_shared<AudioEncoderTestState>();
    state->supportsAudio = supported;
    state->acceptsAudio = configured;
    state->starts = started;
    auto config = createTestConfig(EncoderFactory::Format::MP4);
    config.enableAudio = requested;
    auto result = EncoderFactoryTestAccess::create(config, nullptr, state);
    QCOMPARE(result.success, started);
    QCOMPARE(result.audioEnabled, started && requested && supported && configured);
    QCOMPARE(!result.audioWarning.isEmpty(), started && requested && !(supported && configured));
    QCOMPARE(!result.errorMessage.isEmpty(), !started);
    delete result.nativeEncoder;
    QCOMPARE(state->created.load(), 1);
    QCOMPARE(state->destroyed.load(), 1);
}

void TestEncoderFactory::testAnimatedFormatsDoNotReportAudioFallback()
{
    for (auto format : {EncoderFactory::Format::GIF, EncoderFactory::Format::WebP}) {
        auto config = createTestConfig(format);
        if (format == EncoderFactory::Format::WebP) config.outputPath = m_tempDir->filePath("test.webp");
        config.enableAudio = true;
        auto state = std::make_shared<AudioEncoderTestState>();
        auto result = EncoderFactoryTestAccess::create(config, nullptr, state);
        QVERIFY(result.success);
        QVERIFY(!result.audioEnabled);
        QVERIFY(result.audioWarning.isEmpty());
        QCOMPARE(state->created.load(), 0);
        delete result.gifEncoder;
        delete result.webpEncoder;
    }
}

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

// ============================================================================
// Factory Creation Tests - Native
// ============================================================================

void TestEncoderFactory::testCreateNativeEncoder()
{
    auto config = createTestConfig(EncoderFactory::Format::MP4);

    auto result = EncoderFactory::create(config, this);

    // Native encoder availability depends on platform
    if (EncoderFactory::isNativeEncoderAvailable()) {
        if (!result.success || result.nativeEncoder == nullptr) {
            QSKIP(qPrintable(QString("Native encoder cannot start in this environment: %1")
                                 .arg(result.errorMessage)));
        }
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
        if (!result.success || result.nativeEncoder == nullptr) {
            QSKIP(qPrintable(QString("Native encoder cannot start with audio in this environment: %1")
                                 .arg(result.errorMessage)));
        }
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
        if (!result.success || result.nativeEncoder == nullptr) {
            QSKIP(qPrintable(QString("Native encoder cannot start with custom quality in this environment: %1")
                                 .arg(result.errorMessage)));
        }
        QVERIFY(result.success);
        QVERIFY(result.nativeEncoder != nullptr);

        // Cleanup
        if (result.nativeEncoder) {
            result.nativeEncoder->abort();
            delete result.nativeEncoder;
        }
    }
}

QTEST_MAIN(TestEncoderFactory)
#include "tst_EncoderFactory.moc"
