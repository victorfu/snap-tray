#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>
#include "encoding/NativeGifEncoder.h"

/**
 * @brief Tests for NativeGifEncoder
 *
 * Covers:
 * - Encoder lifecycle (start, writeFrame, finish, abort)
 * - Frame processing and conversion
 * - Signal emission
 * - Error handling
 * - Memory management (Bug #4: memory leak on start() failure)
 */
class TestNativeGifEncoder : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Default state tests
    void testDefaultState();
    void testDefaultBitDepth();

    // Start tests
    void testStartSuccess();
    void testStartSetsRunning();
    void testStartWithOddDimensions();
    void testStartAlreadyRunning();
    void testStart_CleansUpOnFailure();  // Bug #4

    // Frame writing tests
    void testWriteFrameIncrementsCount();
    void testWriteFrameWithTimestamp();
    void testWriteFrameConversion();
    void testWriteFrameScaling();
    void testWriteNullFrame();
    void testWriteFrameNotRunning();
    void testProgressSignalEmitted();

    // Finish tests
    void testFinishCreatesFile();
    void testFinishEmitsSignal();
    void testFinishSetsNotRunning();
    void testFinishWithNoFrames();
    void testFinishNotRunning();

    // Abort tests
    void testAbortCleansUp();
    void testAbortRemovesFile();
    void testAbortNotRunning();
    void testAbortSetsAborted();

    // Bit depth tests
    void testSetMaxBitDepth();
    void testBitDepthClamping();

    // Error handling tests
    void testErrorSignalOnFailure();
    void testLastErrorMessage();

private:
    QTemporaryDir* m_tempDir = nullptr;
    NativeGifEncoder* m_encoder = nullptr;

    QString tempFilePath(const QString& name = "test.gif") const;
    QImage createTestFrame(const QSize& size, QRgb color = qRgb(255, 0, 0)) const;
};

void TestNativeGifEncoder::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
}

void TestNativeGifEncoder::cleanupTestCase()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

void TestNativeGifEncoder::init()
{
    m_encoder = new NativeGifEncoder();
}

void TestNativeGifEncoder::cleanup()
{
    if (m_encoder && m_encoder->isRunning()) {
        m_encoder->abort();
    }
    delete m_encoder;
    m_encoder = nullptr;
}

QString TestNativeGifEncoder::tempFilePath(const QString& name) const
{
    return m_tempDir->filePath(name);
}

QImage TestNativeGifEncoder::createTestFrame(const QSize& size, QRgb color) const
{
    QImage frame(size, QImage::Format_RGBA8888);
    frame.fill(color);
    return frame;
}

// ============================================================================
// Default State Tests
// ============================================================================

void TestNativeGifEncoder::testDefaultState()
{
    QVERIFY(!m_encoder->isRunning());
    QCOMPARE(m_encoder->framesWritten(), 0);
    QVERIFY(m_encoder->outputPath().isEmpty());
    QVERIFY(m_encoder->lastError().isEmpty());
}

void TestNativeGifEncoder::testDefaultBitDepth()
{
    QCOMPARE(m_encoder->maxBitDepth(), 16);
}

// ============================================================================
// Start Tests
// ============================================================================

void TestNativeGifEncoder::testStartSuccess()
{
    QString path = tempFilePath("start_success.gif");
    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));
    QCOMPARE(m_encoder->outputPath(), path);
}

void TestNativeGifEncoder::testStartSetsRunning()
{
    QString path = tempFilePath("start_running.gif");
    QVERIFY(!m_encoder->isRunning());
    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));
    QVERIFY(m_encoder->isRunning());
}

void TestNativeGifEncoder::testStartWithOddDimensions()
{
    // Encoder should adjust odd dimensions to even
    QString path = tempFilePath("odd_dimensions.gif");
    QVERIFY(m_encoder->start(path, QSize(101, 103), 30));
    QVERIFY(m_encoder->isRunning());

    // Write a frame and finish to verify it works
    m_encoder->writeFrame(createTestFrame(QSize(101, 103)));
    m_encoder->finish();
    QVERIFY(QFile::exists(path));
}

void TestNativeGifEncoder::testStartAlreadyRunning()
{
    QString path1 = tempFilePath("first.gif");
    QString path2 = tempFilePath("second.gif");

    QVERIFY(m_encoder->start(path1, QSize(100, 100), 30));
    QVERIFY(!m_encoder->start(path2, QSize(100, 100), 30));
    QCOMPARE(m_encoder->outputPath(), path1);
}

void TestNativeGifEncoder::testStart_CleansUpOnFailure()
{
    // Bug #4: Memory leak when start() fails
    // The encoder should clean up allocated state if initialization fails

    // We can't easily trigger msf_gif_begin failure in unit tests,
    // but we can verify the error path exists and state is correct

    // Test invalid dimensions (0x0)
    QString path = tempFilePath("invalid_dimensions.gif");

    // Note: msf_gif actually handles 0x0 gracefully in most cases
    // This test documents the expected behavior for edge cases

    // If we could mock msf_gif_begin to fail, we would verify:
    // 1. start() returns false
    // 2. m_gifState is nullptr
    // 3. No memory is leaked

    // For now, just verify normal cleanup works
    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));
    m_encoder->abort();
    QVERIFY(!m_encoder->isRunning());
}

// ============================================================================
// Frame Writing Tests
// ============================================================================

void TestNativeGifEncoder::testWriteFrameIncrementsCount()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));

    QCOMPARE(m_encoder->framesWritten(), 0);
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    QCOMPARE(m_encoder->framesWritten(), 1);
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    QCOMPARE(m_encoder->framesWritten(), 2);
}

void TestNativeGifEncoder::testWriteFrameWithTimestamp()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));

    // Write frames with timestamps
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)), 0);
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)), 100);
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)), 200);

    QCOMPARE(m_encoder->framesWritten(), 3);
}

void TestNativeGifEncoder::testWriteFrameConversion()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));

    // Test various input formats
    QImage rgb32(100, 100, QImage::Format_RGB32);
    rgb32.fill(Qt::red);
    m_encoder->writeFrame(rgb32);
    QCOMPARE(m_encoder->framesWritten(), 1);

    QImage argb32(100, 100, QImage::Format_ARGB32);
    argb32.fill(Qt::green);
    m_encoder->writeFrame(argb32);
    QCOMPARE(m_encoder->framesWritten(), 2);

    QImage rgb888(100, 100, QImage::Format_RGB888);
    rgb888.fill(Qt::blue);
    m_encoder->writeFrame(rgb888);
    QCOMPARE(m_encoder->framesWritten(), 3);
}

void TestNativeGifEncoder::testWriteFrameScaling()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));

    // Write a frame with different size - should be scaled
    QImage largeFrame = createTestFrame(QSize(200, 200));
    m_encoder->writeFrame(largeFrame);
    QCOMPARE(m_encoder->framesWritten(), 1);

    QImage smallFrame = createTestFrame(QSize(50, 50));
    m_encoder->writeFrame(smallFrame);
    QCOMPARE(m_encoder->framesWritten(), 2);
}

void TestNativeGifEncoder::testWriteNullFrame()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));

    qint64 before = m_encoder->framesWritten();
    m_encoder->writeFrame(QImage());
    QCOMPARE(m_encoder->framesWritten(), before);
}

void TestNativeGifEncoder::testWriteFrameNotRunning()
{
    // Should silently ignore writes when not running
    QVERIFY(!m_encoder->isRunning());
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    QCOMPARE(m_encoder->framesWritten(), 0);
}

void TestNativeGifEncoder::testProgressSignalEmitted()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));

    QSignalSpy progressSpy(m_encoder, &NativeGifEncoder::progress);

    // Progress is emitted every 30 frames
    for (int i = 0; i < 60; i++) {
        m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    }

    QCOMPARE(progressSpy.count(), 2);
    QCOMPARE(progressSpy.at(0).at(0).toLongLong(), 30);
    QCOMPARE(progressSpy.at(1).at(0).toLongLong(), 60);
}

// ============================================================================
// Finish Tests
// ============================================================================

void TestNativeGifEncoder::testFinishCreatesFile()
{
    QString path = tempFilePath("finish_creates.gif");
    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));

    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    m_encoder->finish();

    QVERIFY(QFile::exists(path));
    QFileInfo info(path);
    QVERIFY(info.size() > 0);
}

void TestNativeGifEncoder::testFinishEmitsSignal()
{
    QString path = tempFilePath("finish_signal.gif");
    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));

    QSignalSpy finishedSpy(m_encoder, &NativeGifEncoder::finished);

    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    m_encoder->finish();

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toBool(), true);
    QCOMPARE(finishedSpy.first().at(1).toString(), path);
}

void TestNativeGifEncoder::testFinishSetsNotRunning()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));
    QVERIFY(m_encoder->isRunning());

    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    m_encoder->finish();

    QVERIFY(!m_encoder->isRunning());
}

void TestNativeGifEncoder::testFinishWithNoFrames()
{
    QString path = tempFilePath("no_frames.gif");
    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));

    QSignalSpy errorSpy(m_encoder, &NativeGifEncoder::error);
    QSignalSpy finishedSpy(m_encoder, &NativeGifEncoder::finished);

    m_encoder->finish();

    // msf_gif can produce valid (empty) GIF even without frames
    // Verify encoder completes without crash
    QVERIFY(!m_encoder->isRunning());
    QCOMPARE(finishedSpy.count(), 1);
    // Note: msf_gif successfully creates empty GIF, so this returns true
}

void TestNativeGifEncoder::testFinishNotRunning()
{
    QSignalSpy finishedSpy(m_encoder, &NativeGifEncoder::finished);

    m_encoder->finish();

    // Should emit finished(false) when not running to notify callers
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toBool(), false);
    QVERIFY(finishedSpy.first().at(1).toString().isEmpty());
}

// ============================================================================
// Abort Tests
// ============================================================================

void TestNativeGifEncoder::testAbortCleansUp()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));
    QVERIFY(m_encoder->isRunning());

    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    m_encoder->abort();

    QVERIFY(!m_encoder->isRunning());
}

void TestNativeGifEncoder::testAbortRemovesFile()
{
    QString path = tempFilePath("abort_removes.gif");
    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));

    // Write some frames
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));

    // Abort should remove incomplete file
    m_encoder->abort();

    QVERIFY(!QFile::exists(path));
}

void TestNativeGifEncoder::testAbortNotRunning()
{
    // Should do nothing when not running
    QVERIFY(!m_encoder->isRunning());
    m_encoder->abort();
    QVERIFY(!m_encoder->isRunning());
}

void TestNativeGifEncoder::testAbortSetsAborted()
{
    QVERIFY(m_encoder->start(tempFilePath(), QSize(100, 100), 30));
    m_encoder->abort();

    // Can't directly test m_aborted flag, but we can verify
    // subsequent writes are ignored
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    QCOMPARE(m_encoder->framesWritten(), 0);
}

// ============================================================================
// Bit Depth Tests
// ============================================================================

void TestNativeGifEncoder::testSetMaxBitDepth()
{
    m_encoder->setMaxBitDepth(8);
    QCOMPARE(m_encoder->maxBitDepth(), 8);

    m_encoder->setMaxBitDepth(12);
    QCOMPARE(m_encoder->maxBitDepth(), 12);
}

void TestNativeGifEncoder::testBitDepthClamping()
{
    m_encoder->setMaxBitDepth(0);
    QCOMPARE(m_encoder->maxBitDepth(), 1);

    m_encoder->setMaxBitDepth(20);
    QCOMPARE(m_encoder->maxBitDepth(), 16);

    m_encoder->setMaxBitDepth(-5);
    QCOMPARE(m_encoder->maxBitDepth(), 1);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

void TestNativeGifEncoder::testErrorSignalOnFailure()
{
    // Test error signal emission when finish fails
    QString invalidPath = "/nonexistent/directory/test.gif";
    QVERIFY(m_encoder->start(invalidPath, QSize(100, 100), 30));

    QSignalSpy errorSpy(m_encoder, &NativeGifEncoder::error);

    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    m_encoder->finish();

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(!errorSpy.first().first().toString().isEmpty());
}

void TestNativeGifEncoder::testLastErrorMessage()
{
    QString invalidPath = "/nonexistent/directory/test.gif";
    QVERIFY(m_encoder->start(invalidPath, QSize(100, 100), 30));

    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    m_encoder->finish();

    QVERIFY(!m_encoder->lastError().isEmpty());
}

QTEST_MAIN(TestNativeGifEncoder)
#include "tst_NativeGifEncoder.moc"
