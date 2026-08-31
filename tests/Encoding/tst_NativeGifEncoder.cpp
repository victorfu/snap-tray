#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>
#include "encoding/NativeGifEncoder.h"

#include <numeric>

namespace {

QList<int> gifFrameDelays(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QByteArray data = file.readAll();
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
    QList<int> delays;
    for (qsizetype i = 0; i + 7 < data.size(); ++i) {
        if (bytes[i] == 0x21 && bytes[i + 1] == 0xf9 && bytes[i + 2] == 0x04) {
            delays.append(bytes[i + 4] | (bytes[i + 5] << 8));
            i += 7;
        }
    }
    return delays;
}

} // namespace

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
    void testTimestampDelayBelongsToPreviousFrame();
    void testDefaultFrameRatePreservesCentisecondRemainder_data();
    void testDefaultFrameRatePreservesCentisecondRemainder();
    void testTimestampQuantizationPreservesDuration_data();
    void testTimestampQuantizationPreservesDuration();

    // Finish tests
    void testFinishCreatesFile();
    void testFinishEmitsSignal();
    void testFinishSetsNotRunning();
    void testFinishWithNoFrames();
    void testFinishWithNoFramesPreservesExistingFile();
    void testFinishNotRunning();
    void testFinishFlushesSingleFrame();

    // Abort tests
    void testAbortCleansUp();
    void testAbortRemovesFile();
    void testAbortPreservesExistingFile();
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

void TestNativeGifEncoder::testTimestampDelayBelongsToPreviousFrame()
{
    const QString path = tempFilePath("timestamp_previous_frame.gif");
    QVERIFY(m_encoder->start(path, QSize(2, 2), 20));

    m_encoder->writeFrame(createTestFrame(QSize(2, 2), qRgb(255, 0, 0)), 0);
    m_encoder->writeFrame(createTestFrame(QSize(2, 2), qRgb(0, 255, 0)), 15);
    m_encoder->writeFrame(createTestFrame(QSize(2, 2), qRgb(0, 0, 255)), 35);
    m_encoder->writeFrame(createTestFrame(QSize(2, 2), qRgb(255, 255, 0)), 60);
    m_encoder->finish();

    QCOMPARE(gifFrameDelays(path), QList<int>({1, 2, 3, 5}));
}

void TestNativeGifEncoder::testDefaultFrameRatePreservesCentisecondRemainder_data()
{
    QTest::addColumn<int>("frameRate");

    QTest::newRow("15 fps") << 15;
    QTest::newRow("24 fps") << 24;
    QTest::newRow("30 fps") << 30;
}

void TestNativeGifEncoder::testDefaultFrameRatePreservesCentisecondRemainder()
{
    QFETCH(int, frameRate);

    const QString path = tempFilePath(QString("default_%1_fps.gif").arg(frameRate));
    QVERIFY(m_encoder->start(path, QSize(2, 2), frameRate));
    for (int i = 0; i < frameRate; ++i) {
        m_encoder->writeFrame(createTestFrame(
            QSize(2, 2), qRgb((i * 37) % 256, (i * 71) % 256, (i * 113) % 256)));
    }
    m_encoder->finish();

    const QList<int> delays = gifFrameDelays(path);
    QCOMPARE(delays.size(), frameRate);
    QList<int> expectedDelays;
    int remainder = 0;
    for (int i = 0; i < frameRate; ++i) {
        remainder += 100;
        expectedDelays.append(remainder / frameRate);
        remainder %= frameRate;
    }
    QCOMPARE(delays, expectedDelays);
    QCOMPARE(std::accumulate(delays.cbegin(), delays.cend(), 0), 100);
}

void TestNativeGifEncoder::testTimestampQuantizationPreservesDuration_data()
{
    QTest::addColumn<int>("frameRate");
    QTest::addColumn<int>("expectedCentiseconds");

    QTest::newRow("15 fps") << 15 << 106;
    QTest::newRow("24 fps") << 24 << 104;
    QTest::newRow("30 fps") << 30 << 103;
}

void TestNativeGifEncoder::testTimestampQuantizationPreservesDuration()
{
    QFETCH(int, frameRate);
    QFETCH(int, expectedCentiseconds);

    const QString path = tempFilePath(QString("timestamp_%1_fps.gif").arg(frameRate));
    QVERIFY(m_encoder->start(path, QSize(2, 2), frameRate));
    for (int i = 0; i <= frameRate; ++i) {
        const qint64 timestampMs = static_cast<qint64>(i) * 1000 / frameRate;
        m_encoder->writeFrame(createTestFrame(
            QSize(2, 2), qRgb((i * 41) % 256, (i * 67) % 256, (i * 97) % 256)),
            timestampMs);
    }
    m_encoder->finish();

    const QList<int> delays = gifFrameDelays(path);
    QCOMPARE(delays.size(), frameRate + 1);
    QList<int> expectedDelays;
    int remainder = 0;
    for (int i = 0; i < frameRate; ++i) {
        remainder += 100;
        expectedDelays.append(remainder / frameRate);
        remainder %= frameRate;
    }
    expectedDelays.append(100 / frameRate);
    QCOMPARE(delays, expectedDelays);
    QCOMPARE(std::accumulate(delays.cbegin(), delays.cend(), 0), expectedCentiseconds);
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

    QVERIFY(!m_encoder->isRunning());
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toBool(), false);
    QCOMPARE(finishedSpy.first().at(1).toString(), path);
    QVERIFY(!QFile::exists(path));
    QVERIFY(m_encoder->lastError().contains("no frames"));
}

void TestNativeGifEncoder::testFinishWithNoFramesPreservesExistingFile()
{
    const QString path = tempFilePath("existing_no_frames.gif");
    const QByteArray originalData("existing-user-data");
    QFile existingFile(path);
    QVERIFY(existingFile.open(QIODevice::WriteOnly));
    QCOMPARE(existingFile.write(originalData), static_cast<qint64>(originalData.size()));
    existingFile.close();

    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));
    QSignalSpy finishedSpy(m_encoder, &NativeGifEncoder::finished);
    m_encoder->finish();

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!finishedSpy.first().at(0).toBool());
    QVERIFY(existingFile.open(QIODevice::ReadOnly));
    QCOMPARE(existingFile.readAll(), originalData);
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

void TestNativeGifEncoder::testFinishFlushesSingleFrame()
{
    const QString path = tempFilePath("single_frame_flush.gif");
    QVERIFY(m_encoder->start(path, QSize(2, 2), 24));
    m_encoder->writeFrame(createTestFrame(QSize(2, 2)));
    QCOMPARE(m_encoder->framesWritten(), 1);

    m_encoder->finish();

    QCOMPARE(gifFrameDelays(path), QList<int>({4}));
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

void TestNativeGifEncoder::testAbortPreservesExistingFile()
{
    const QString path = tempFilePath("abort_existing.gif");
    const QByteArray originalData("existing-user-data");
    QFile existingFile(path);
    QVERIFY(existingFile.open(QIODevice::WriteOnly));
    QCOMPARE(existingFile.write(originalData), static_cast<qint64>(originalData.size()));
    existingFile.close();

    QVERIFY(m_encoder->start(path, QSize(100, 100), 30));
    m_encoder->writeFrame(createTestFrame(QSize(100, 100)));
    m_encoder->abort();

    QVERIFY(existingFile.open(QIODevice::ReadOnly));
    QCOMPARE(existingFile.readAll(), originalData);
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
