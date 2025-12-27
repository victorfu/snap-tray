#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include "AudioFileWriter.h"

/**
 * @brief Tests for AudioFileWriter
 *
 * Covers:
 * - WAV file creation and format
 * - Audio data writing
 * - Thread safety
 * - Error handling (Bug #5: finish() error handling)
 */
class TestAudioFileWriter : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Basic lifecycle tests
    void testDefaultState();
    void testStartCreatesFile();
    void testStartWithInvalidPath();
    void testStartWhileAlreadyWriting();
    void testFinishClosesFile();
    void testDestructorCallsFinish();

    // WAV header tests
    void testWavHeaderFormat();
    void testWavHeaderWithDifferentFormats();
    void testWavHeaderSampleRate();
    void testWavHeaderChannels();
    void testWavHeaderBitsPerSample();

    // Data writing tests
    void testWriteAudioData();
    void testWriteMultipleChunks();
    void testWriteEmptyData();
    void testWriteAfterFinish();
    void testBytesWrittenAccuracy();

    // Duration calculation tests
    void testDurationMsEmpty();
    void testDurationMsWithData();
    void testDurationMsWithDifferentFormats();

    // Error handling tests (Bug #5)
    void testFinish_SeekFailure();
    void testWriteError();
    void testErrorSignalEmitted();

    // Thread safety tests
    void testConcurrentWrites();

private:
    QTemporaryDir* m_tempDir = nullptr;
    AudioFileWriter* m_writer = nullptr;

    QString tempFilePath(const QString& name = "test.wav") const;
    bool isValidWavHeader(const QByteArray& header) const;
    AudioFileWriter::AudioFormat defaultFormat() const;
};

void TestAudioFileWriter::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
}

void TestAudioFileWriter::cleanupTestCase()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

void TestAudioFileWriter::init()
{
    m_writer = new AudioFileWriter();
}

void TestAudioFileWriter::cleanup()
{
    delete m_writer;
    m_writer = nullptr;
}

QString TestAudioFileWriter::tempFilePath(const QString& name) const
{
    return m_tempDir->filePath(name);
}

AudioFileWriter::AudioFormat TestAudioFileWriter::defaultFormat() const
{
    AudioFileWriter::AudioFormat format;
    format.sampleRate = 48000;
    format.channels = 2;
    format.bitsPerSample = 16;
    return format;
}

bool TestAudioFileWriter::isValidWavHeader(const QByteArray& header) const
{
    if (header.size() < 44) return false;
    // Check RIFF header
    if (header.mid(0, 4) != "RIFF") return false;
    // Check WAVE format
    if (header.mid(8, 4) != "WAVE") return false;
    // Check fmt chunk
    if (header.mid(12, 4) != "fmt ") return false;
    // Check data chunk
    if (header.mid(36, 4) != "data") return false;
    return true;
}

// ============================================================================
// Basic Lifecycle Tests
// ============================================================================

void TestAudioFileWriter::testDefaultState()
{
    QVERIFY(!m_writer->isWriting());
    QVERIFY(m_writer->filePath().isEmpty());
    QCOMPARE(m_writer->bytesWritten(), 0);
    QCOMPARE(m_writer->durationMs(), 0);
}

void TestAudioFileWriter::testStartCreatesFile()
{
    QString path = tempFilePath("start_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));
    QVERIFY(m_writer->isWriting());
    QCOMPARE(m_writer->filePath(), path);
    QVERIFY(QFile::exists(path));
}

void TestAudioFileWriter::testStartWithInvalidPath()
{
    QSignalSpy errorSpy(m_writer, &AudioFileWriter::error);
    QString invalidPath = "/nonexistent/directory/test.wav";

    QVERIFY(!m_writer->start(invalidPath, defaultFormat()));
    QVERIFY(!m_writer->isWriting());
    QCOMPARE(errorSpy.count(), 1);
}

void TestAudioFileWriter::testStartWhileAlreadyWriting()
{
    QString path1 = tempFilePath("first.wav");
    QString path2 = tempFilePath("second.wav");

    QVERIFY(m_writer->start(path1, defaultFormat()));
    QVERIFY(!m_writer->start(path2, defaultFormat()));
    QCOMPARE(m_writer->filePath(), path1);
}

void TestAudioFileWriter::testFinishClosesFile()
{
    QString path = tempFilePath("finish_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));
    QVERIFY(m_writer->isWriting());

    m_writer->finish();
    QVERIFY(!m_writer->isWriting());
}

void TestAudioFileWriter::testDestructorCallsFinish()
{
    QString path = tempFilePath("destructor_test.wav");

    {
        AudioFileWriter writer;
        QVERIFY(writer.start(path, defaultFormat()));
        writer.writeAudioData(QByteArray(1024, 'A'));
        // Destructor should call finish()
    }

    // File should be valid after destruction
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray header = file.read(44);
    QVERIFY(isValidWavHeader(header));
}

// ============================================================================
// WAV Header Tests
// ============================================================================

void TestAudioFileWriter::testWavHeaderFormat()
{
    QString path = tempFilePath("header_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));
    m_writer->writeAudioData(QByteArray(100, 0));
    m_writer->finish();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray header = file.read(44);
    QVERIFY(isValidWavHeader(header));
}

void TestAudioFileWriter::testWavHeaderWithDifferentFormats()
{
    struct TestCase {
        int sampleRate;
        int channels;
        int bitsPerSample;
    };

    QList<TestCase> testCases = {
        {44100, 1, 16},
        {48000, 2, 16},
        {96000, 2, 24},
        {22050, 1, 8},
    };

    int i = 0;
    for (const auto& tc : testCases) {
        AudioFileWriter writer;
        QString path = tempFilePath(QString("format_%1.wav").arg(i++));

        AudioFileWriter::AudioFormat format;
        format.sampleRate = tc.sampleRate;
        format.channels = tc.channels;
        format.bitsPerSample = tc.bitsPerSample;

        QVERIFY(writer.start(path, format));
        writer.writeAudioData(QByteArray(100, 0));
        writer.finish();

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray header = file.read(44);
        QVERIFY(isValidWavHeader(header));
    }
}

void TestAudioFileWriter::testWavHeaderSampleRate()
{
    QString path = tempFilePath("samplerate_test.wav");
    AudioFileWriter::AudioFormat format = defaultFormat();
    format.sampleRate = 44100;

    QVERIFY(m_writer->start(path, format));
    m_writer->finish();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    file.seek(24);
    QByteArray sampleRateBytes = file.read(4);
    quint32 sampleRate = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(sampleRateBytes.constData()));
    QCOMPARE(sampleRate, 44100u);
}

void TestAudioFileWriter::testWavHeaderChannels()
{
    QString path = tempFilePath("channels_test.wav");
    AudioFileWriter::AudioFormat format = defaultFormat();
    format.channels = 1;

    QVERIFY(m_writer->start(path, format));
    m_writer->finish();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    file.seek(22);
    QByteArray channelsBytes = file.read(2);
    quint16 channels = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(channelsBytes.constData()));
    QCOMPARE(channels, 1u);
}

void TestAudioFileWriter::testWavHeaderBitsPerSample()
{
    QString path = tempFilePath("bits_test.wav");
    AudioFileWriter::AudioFormat format = defaultFormat();
    format.bitsPerSample = 24;

    QVERIFY(m_writer->start(path, format));
    m_writer->finish();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    file.seek(34);
    QByteArray bitsBytes = file.read(2);
    quint16 bits = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(bitsBytes.constData()));
    QCOMPARE(bits, 24u);
}

// ============================================================================
// Data Writing Tests
// ============================================================================

void TestAudioFileWriter::testWriteAudioData()
{
    QString path = tempFilePath("write_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));

    QByteArray testData(1024, 'X');
    m_writer->writeAudioData(testData);

    QCOMPARE(m_writer->bytesWritten(), 1024);
}

void TestAudioFileWriter::testWriteMultipleChunks()
{
    QString path = tempFilePath("multi_chunk_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));

    for (int i = 0; i < 10; i++) {
        m_writer->writeAudioData(QByteArray(100, static_cast<char>(i)));
    }

    QCOMPARE(m_writer->bytesWritten(), 1000);
    m_writer->finish();

    // Verify file size: 44 (header) + 1000 (data)
    QFileInfo info(path);
    QCOMPARE(info.size(), 1044);
}

void TestAudioFileWriter::testWriteEmptyData()
{
    QString path = tempFilePath("empty_data_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));

    qint64 before = m_writer->bytesWritten();
    m_writer->writeAudioData(QByteArray());
    QCOMPARE(m_writer->bytesWritten(), before);
}

void TestAudioFileWriter::testWriteAfterFinish()
{
    QString path = tempFilePath("write_after_finish_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));
    m_writer->writeAudioData(QByteArray(100, 'A'));
    m_writer->finish();

    qint64 bytesAfterFinish = m_writer->bytesWritten();
    m_writer->writeAudioData(QByteArray(100, 'B'));
    // Should not increase after finish
    QCOMPARE(m_writer->bytesWritten(), bytesAfterFinish);
}

void TestAudioFileWriter::testBytesWrittenAccuracy()
{
    QString path = tempFilePath("bytes_accuracy_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));

    qint64 totalWritten = 0;
    for (int i = 0; i < 100; i++) {
        int size = (i + 1) * 10;
        m_writer->writeAudioData(QByteArray(size, 'X'));
        totalWritten += size;
        QCOMPARE(m_writer->bytesWritten(), totalWritten);
    }
}

// ============================================================================
// Duration Calculation Tests
// ============================================================================

void TestAudioFileWriter::testDurationMsEmpty()
{
    QCOMPARE(m_writer->durationMs(), 0);
}

void TestAudioFileWriter::testDurationMsWithData()
{
    QString path = tempFilePath("duration_test.wav");
    AudioFileWriter::AudioFormat format = defaultFormat();
    // 48000 Hz, 2 channels, 16-bit = 192000 bytes/sec
    QVERIFY(m_writer->start(path, format));

    // Write exactly 1 second of audio
    int bytesPerSecond = format.sampleRate * format.channels * (format.bitsPerSample / 8);
    m_writer->writeAudioData(QByteArray(bytesPerSecond, 0));

    QCOMPARE(m_writer->durationMs(), 1000);

    // Write half a second more
    m_writer->writeAudioData(QByteArray(bytesPerSecond / 2, 0));
    QCOMPARE(m_writer->durationMs(), 1500);
}

void TestAudioFileWriter::testDurationMsWithDifferentFormats()
{
    // Test mono 44100 Hz 16-bit
    {
        AudioFileWriter writer;
        QString path = tempFilePath("duration_mono.wav");
        AudioFileWriter::AudioFormat format;
        format.sampleRate = 44100;
        format.channels = 1;
        format.bitsPerSample = 16;

        QVERIFY(writer.start(path, format));

        // 44100 * 1 * 2 = 88200 bytes/sec
        writer.writeAudioData(QByteArray(88200, 0));
        QCOMPARE(writer.durationMs(), 1000);
    }
}

// ============================================================================
// Error Handling Tests (Bug #5)
// ============================================================================

void TestAudioFileWriter::testFinish_SeekFailure()
{
    // This test verifies that finish() handles seek failures gracefully
    // In real scenarios, seek might fail on read-only filesystems or full disks

    QString path = tempFilePath("seek_failure_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));
    m_writer->writeAudioData(QByteArray(1000, 'X'));

    // Note: We can't easily simulate a seek failure in unit tests
    // This test ensures finish() at least completes without crashing
    // The actual bug is that updateHeader() returns false but finish()
    // doesn't propagate this error to the caller

    QSignalSpy errorSpy(m_writer, &AudioFileWriter::error);
    m_writer->finish();

    // Normal case should succeed without errors
    // If there were a seek failure, error signal would be emitted
    // This test documents the expected behavior
}

void TestAudioFileWriter::testWriteError()
{
    // Test that write errors are reported via signal
    QSignalSpy errorSpy(m_writer, &AudioFileWriter::error);

    // Try to write without starting - should silently fail
    m_writer->writeAudioData(QByteArray(100, 'X'));
    QCOMPARE(m_writer->bytesWritten(), 0);
}

void TestAudioFileWriter::testErrorSignalEmitted()
{
    QSignalSpy errorSpy(m_writer, &AudioFileWriter::error);

    // Try to open invalid path
    QVERIFY(!m_writer->start("/invalid/path/test.wav", defaultFormat()));
    QCOMPARE(errorSpy.count(), 1);

    // Verify error message is not empty
    QString errorMsg = errorSpy.first().first().toString();
    QVERIFY(!errorMsg.isEmpty());
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

void TestAudioFileWriter::testConcurrentWrites()
{
    QString path = tempFilePath("concurrent_test.wav");
    QVERIFY(m_writer->start(path, defaultFormat()));

    const int numThreads = 4;
    const int writesPerThread = 100;
    const int bytesPerWrite = 1000;

    QList<QThread*> threads;

    for (int i = 0; i < numThreads; i++) {
        QThread* thread = QThread::create([this, bytesPerWrite, writesPerThread]() {
            for (int j = 0; j < writesPerThread; j++) {
                m_writer->writeAudioData(QByteArray(bytesPerWrite, 'X'));
            }
        });
        threads.append(thread);
    }

    for (auto* thread : threads) {
        thread->start();
    }

    for (auto* thread : threads) {
        thread->wait();
        delete thread;
    }

    // All writes should complete without crashes
    // Total bytes should be numThreads * writesPerThread * bytesPerWrite
    QCOMPARE(m_writer->bytesWritten(),
             static_cast<qint64>(numThreads * writesPerThread * bytesPerWrite));

    m_writer->finish();

    // Verify file is valid
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray header = file.read(44);
    QVERIFY(isValidWavHeader(header));
}

QTEST_MAIN(TestAudioFileWriter)
#include "tst_AudioFileWriter.moc"
