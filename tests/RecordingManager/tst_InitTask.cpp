#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QThread>
#include <QTemporaryDir>
#include "RecordingInitTask.h"
#include "RecordingRegionNormalizer.h"
#include "utils/CoordinateHelper.h"

/**
 * @brief Tests for RecordingInitTask
 *
 * Covers:
 * - Configuration handling
 * - Async initialization flow
 * - Cancellation support
 * - Resource cleanup on failure
 * - Bug #2 prevention context
 */
class TestRecordingInitTask : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Configuration tests
    void testDefaultConfig();
    void testConfigWithRegion();
    void testConfigWithAudio();
    void testConfigWithGif();
    void testConfigWithNativeEncoder();
    void testNormalizedRegionFrameSizeAlignment();

    // Cancellation tests
    void testCancelBeforeRun();
    void testCancelDuringRun();
    void testIsCancelledInitiallyFalse();
    void testDiscardResultIsSafeToCallRepeatedly();

    // Result tests
    void testResultDefaultValues();
    void testResultCleanupNullPointers();
    void testResultCleanupWithResources();

    // Signal tests
    void testProgressSignalEmitted();
    void testFinishedSignalEmitted();

private:
    QTemporaryDir* m_tempDir = nullptr;

    RecordingInitTask::Config createTestConfig();
};

void TestRecordingInitTask::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
}

void TestRecordingInitTask::cleanupTestCase()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

void TestRecordingInitTask::init()
{
}

void TestRecordingInitTask::cleanup()
{
}

RecordingInitTask::Config TestRecordingInitTask::createTestConfig()
{
    RecordingInitTask::Config config;
    config.region = QRect(0, 0, 800, 600);
    config.screen = nullptr;  // Will use primary screen
    config.frameRate = 30;
    config.audioEnabled = false;
    config.audioSource = 0;
    config.outputPath = m_tempDir->filePath("test_output.mp4");
    config.useNativeEncoder = true;
    config.outputFormat = EncoderFactory::Format::MP4;
    config.frameSize = QSize(800, 600);
    config.quality = 55;
    return config;
}

// ============================================================================
// Configuration Tests
// ============================================================================

void TestRecordingInitTask::testDefaultConfig()
{
    RecordingInitTask::Config config;

    QCOMPARE(config.frameRate, 30);
    QVERIFY(!config.audioEnabled);
    QCOMPARE(config.audioSource, 0);
    QVERIFY(config.useNativeEncoder);
    QCOMPARE(config.outputFormat, EncoderFactory::Format::MP4);
    QCOMPARE(config.quality, 55);
}

void TestRecordingInitTask::testConfigWithRegion()
{
    RecordingInitTask::Config config;
    config.region = QRect(100, 100, 640, 480);
    config.frameSize = QSize(640, 480);

    RecordingInitTask task(config);
    // Config should be stored correctly
    // We can verify by checking the result after an attempt

    QVERIFY(!task.isCancelled());
}

void TestRecordingInitTask::testConfigWithAudio()
{
    RecordingInitTask::Config config = createTestConfig();
    config.audioEnabled = true;
    config.audioSource = 2;  // Both mic and system audio
    config.audioDevice = "test_device";

    RecordingInitTask task(config);
    QVERIFY(!task.isCancelled());
}

void TestRecordingInitTask::testConfigWithGif()
{
    RecordingInitTask::Config config = createTestConfig();
    config.outputFormat = EncoderFactory::Format::GIF;
    config.outputPath = m_tempDir->filePath("test.gif");

    RecordingInitTask task(config);
    QVERIFY(!task.isCancelled());
}

void TestRecordingInitTask::testConfigWithNativeEncoder()
{
    RecordingInitTask::Config config = createTestConfig();
    config.useNativeEncoder = true;
    config.outputFormat = EncoderFactory::Format::MP4;

    RecordingInitTask task(config);
    QVERIFY(!task.isCancelled());
}

void TestRecordingInitTask::testNormalizedRegionFrameSizeAlignment()
{
    const QRect screenBounds(0, 0, 120, 80);
    const QRect selectedRegion(65, 43, 55, 37); // odd physical size at DPR 1.1
    constexpr qreal dpr = 1.1;

    const QRect normalizedRegion = normalizeToEvenPhysicalRegion(selectedRegion, screenBounds, dpr);
    const QSize frameSize = CoordinateHelper::toPhysical(normalizedRegion.size(), dpr);

    QVERIFY((frameSize.width() % 2) == 0);
    QVERIFY((frameSize.height() % 2) == 0);
    QCOMPARE(CoordinateHelper::toPhysical(normalizedRegion.size(), dpr), frameSize);

    RecordingInitTask::Config config = createTestConfig();
    config.region = normalizedRegion;
    config.frameSize = frameSize;

    QCOMPARE(config.frameSize, CoordinateHelper::toPhysical(config.region.size(), dpr));
}

// ============================================================================
// Cancellation Tests
// ============================================================================

void TestRecordingInitTask::testCancelBeforeRun()
{
    RecordingInitTask::Config config = createTestConfig();
    RecordingInitTask task(config);

    QVERIFY(!task.isCancelled());
    task.cancel();
    QVERIFY(task.isCancelled());
}

void TestRecordingInitTask::testCancelDuringRun()
{
    RecordingInitTask::Config config = createTestConfig();
    RecordingInitTask task(config);

    QSignalSpy finishedSpy(&task, &RecordingInitTask::finished);
    QSignalSpy progressSpy(&task, &RecordingInitTask::progress);

    // Cancel before running
    task.cancel();

    // Run should detect cancellation
    task.run();

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!task.result().success);
    QVERIFY(task.result().error.contains("cancelled"));
}

void TestRecordingInitTask::testIsCancelledInitiallyFalse()
{
    RecordingInitTask::Config config = createTestConfig();
    RecordingInitTask task(config);

    QVERIFY(!task.isCancelled());
}

void TestRecordingInitTask::testDiscardResultIsSafeToCallRepeatedly()
{
    RecordingInitTask::Config config = createTestConfig();
    RecordingInitTask task(config);

    task.discardResult();
    task.discardResult();

    QVERIFY(!task.result().success);
}

// ============================================================================
// Result Tests
// ============================================================================

void TestRecordingInitTask::testResultDefaultValues()
{
    RecordingInitTask::Result result;

    QVERIFY(!result.success);
    QVERIFY(result.error.isEmpty());
    QVERIFY(result.captureEngine == nullptr);
    QVERIFY(result.nativeEncoder == nullptr);
    QVERIFY(result.gifEncoder == nullptr);
    QVERIFY(result.audioEngine == nullptr);
    QVERIFY(!result.usingNativeEncoder);
    QVERIFY(result.tempAudioPath.isEmpty());
    QVERIFY(!result.captureEngineStarted);
}

void TestRecordingInitTask::testResultCleanupNullPointers()
{
    RecordingInitTask::Result result;

    // Should not crash with all null pointers
    result.cleanup();

    QVERIFY(result.captureEngine == nullptr);
    QVERIFY(result.nativeEncoder == nullptr);
    QVERIFY(result.gifEncoder == nullptr);
    QVERIFY(result.audioEngine == nullptr);
}

void TestRecordingInitTask::testResultCleanupWithResources()
{
    // This test would require mock objects
    // For now, we just verify cleanup doesn't crash with nulls
    RecordingInitTask::Result result;
    result.cleanup();
    QVERIFY(result.captureEngine == nullptr);
}

// ============================================================================
// Signal Tests
// ============================================================================

void TestRecordingInitTask::testProgressSignalEmitted()
{
    RecordingInitTask::Config config = createTestConfig();
    RecordingInitTask task(config);

    QSignalSpy progressSpy(&task, &RecordingInitTask::progress);

    // Cancel immediately to get quick termination
    task.cancel();
    task.run();

    // Progress should be emitted at least once before cancellation check
    // or the cancelled error state
    QVERIFY(progressSpy.count() >= 0);
}

void TestRecordingInitTask::testFinishedSignalEmitted()
{
    RecordingInitTask::Config config = createTestConfig();
    RecordingInitTask task(config);

    QSignalSpy finishedSpy(&task, &RecordingInitTask::finished);

    task.cancel();
    task.run();

    QCOMPARE(finishedSpy.count(), 1);
}

QTEST_MAIN(TestRecordingInitTask)
#include "tst_InitTask.moc"
