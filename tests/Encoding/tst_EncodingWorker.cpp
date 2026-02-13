#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QtConcurrent>
#include <QMutex>
#include <QWaitCondition>

#include "encoding/EncodingWorker.h"
#include "IVideoEncoder.h"

#include <atomic>
#include <stdexcept>

namespace {
QImage createTestFrame()
{
    QImage frame(16, 16, QImage::Format_ARGB32);
    frame.fill(Qt::red);
    return frame;
}
}

class ThrowingVideoEncoder final : public IVideoEncoder
{
public:
    explicit ThrowingVideoEncoder(bool throwOnFrame, bool throwOnAudio, QObject* parent = nullptr)
        : IVideoEncoder(parent)
        , m_throwOnFrame(throwOnFrame)
        , m_throwOnAudio(throwOnAudio)
    {
    }

    bool isAvailable() const override { return true; }
    QString encoderName() const override { return QStringLiteral("ThrowingVideoEncoder"); }

    bool start(const QString& outputPath, const QSize& frameSize, int frameRate) override
    {
        Q_UNUSED(frameSize);
        Q_UNUSED(frameRate);
        m_outputPath = outputPath;
        m_running = true;
        return true;
    }

    void writeFrame(const QImage& frame, qint64 timestampMs) override
    {
        Q_UNUSED(frame);
        Q_UNUSED(timestampMs);
        if (m_throwOnFrame) {
            throw std::runtime_error("mock frame write failure");
        }
        ++m_framesWritten;
    }

    void finish() override
    {
        ++m_finishCalls;
        m_running = false;
        emit finished(true, m_outputPath);
    }

    void abort() override
    {
        m_running = false;
    }

    bool isRunning() const override { return m_running; }
    QString lastError() const override { return QString(); }
    qint64 framesWritten() const override { return m_framesWritten; }
    QString outputPath() const override { return m_outputPath; }
    int finishCallCount() const { return m_finishCalls.load(); }

    void writeAudioSamples(const QByteArray& pcmData, qint64 timestampMs) override
    {
        Q_UNUSED(pcmData);
        Q_UNUSED(timestampMs);
        if (m_throwOnAudio) {
            throw std::runtime_error("mock audio write failure");
        }
    }

private:
    const bool m_throwOnFrame;
    const bool m_throwOnAudio;
    bool m_running = true;
    qint64 m_framesWritten = 0;
    std::atomic<int> m_finishCalls{0};
    QString m_outputPath;
};

class BlockingVideoEncoder final : public IVideoEncoder
{
public:
    explicit BlockingVideoEncoder(QObject* parent = nullptr)
        : IVideoEncoder(parent)
    {
    }

    bool isAvailable() const override { return true; }
    QString encoderName() const override { return QStringLiteral("BlockingVideoEncoder"); }

    bool start(const QString& outputPath, const QSize& frameSize, int frameRate) override
    {
        Q_UNUSED(frameSize);
        Q_UNUSED(frameRate);
        m_outputPath = outputPath;
        m_running = true;
        return true;
    }

    void writeFrame(const QImage& frame, qint64 timestampMs) override
    {
        Q_UNUSED(frame);
        Q_UNUSED(timestampMs);

        QMutexLocker locker(&m_mutex);
        m_writeEntered = true;
        m_enteredCondition.wakeAll();
        while (!m_releaseWrite) {
            m_releaseCondition.wait(&m_mutex);
        }
        ++m_writeCount;
    }

    void finish() override
    {
        m_running = false;
        emit finished(true, m_outputPath);
    }

    void abort() override
    {
        m_running = false;
        releaseWrite();
    }

    bool isRunning() const override { return m_running; }
    QString lastError() const override { return QString(); }
    qint64 framesWritten() const override { return m_writeCount.load(); }
    QString outputPath() const override { return m_outputPath; }

    bool waitUntilWriteEntered(int timeoutMs)
    {
        QMutexLocker locker(&m_mutex);
        if (m_writeEntered) {
            return true;
        }
        const bool signaled = m_enteredCondition.wait(&m_mutex, timeoutMs);
        return signaled && m_writeEntered;
    }

    void releaseWrite()
    {
        QMutexLocker locker(&m_mutex);
        m_releaseWrite = true;
        m_releaseCondition.wakeAll();
    }

    int writeCount() const
    {
        return m_writeCount.load();
    }

private:
    bool m_running = true;
    QString m_outputPath;
    mutable QMutex m_mutex;
    QWaitCondition m_enteredCondition;
    QWaitCondition m_releaseCondition;
    bool m_writeEntered = false;
    bool m_releaseWrite = false;
    std::atomic<int> m_writeCount{0};
};

class TestEncodingWorker : public QObject
{
    Q_OBJECT

private slots:
    void testFrameExceptionStopsWorker();
    void testAudioExceptionStopsWorker();
    void testRequestFinishAfterFailureDoesNotEmitFinished();
    void testStopDuringFrameProcessingResetsProcessingState();
};

void TestEncodingWorker::testFrameExceptionStopsWorker()
{
    EncodingWorker worker;
    worker.setEncoderType(EncodingWorker::EncoderType::Video);
    worker.setVideoEncoder(new ThrowingVideoEncoder(true, false));
    QVERIFY(worker.start());

    QSignalSpy errorSpy(&worker, &EncodingWorker::error);
    QSignalSpy finishedSpy(&worker, &EncodingWorker::finished);

    EncodingWorker::FrameData frameData;
    frameData.frame = createTestFrame();
    frameData.timestampMs = 10;
    QVERIFY(worker.enqueueFrame(frameData));

    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() == 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!worker.isProcessing(), 3000);
    QVERIFY(!worker.isRunning());
    QCOMPARE(worker.queueDepth(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(!worker.enqueueFrame(frameData));

    const QString errorMessage = errorSpy.first().first().toString();
    QVERIFY(errorMessage.contains("frame encoding"));
}

void TestEncodingWorker::testAudioExceptionStopsWorker()
{
    EncodingWorker worker;
    worker.setEncoderType(EncodingWorker::EncoderType::Video);
    worker.setVideoEncoder(new ThrowingVideoEncoder(false, true));
    QVERIFY(worker.start());

    QSignalSpy errorSpy(&worker, &EncodingWorker::error);
    QSignalSpy finishedSpy(&worker, &EncodingWorker::finished);

    worker.writeAudioSamples(QByteArray(128, char(0x7f)), 20);

    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() == 1, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!worker.isProcessing(), 3000);
    QVERIFY(!worker.isRunning());
    QCOMPARE(worker.queueDepth(), 0);
    QCOMPARE(finishedSpy.count(), 0);

    const QString errorMessage = errorSpy.first().first().toString();
    QVERIFY(errorMessage.contains("audio encoding"));
}

void TestEncodingWorker::testRequestFinishAfterFailureDoesNotEmitFinished()
{
    EncodingWorker worker;
    auto* encoder = new ThrowingVideoEncoder(true, false);
    worker.setEncoderType(EncodingWorker::EncoderType::Video);
    worker.setVideoEncoder(encoder);
    QVERIFY(worker.start());

    QSignalSpy errorSpy(&worker, &EncodingWorker::error);
    QSignalSpy finishedSpy(&worker, &EncodingWorker::finished);

    EncodingWorker::FrameData frameData;
    frameData.frame = createTestFrame();
    frameData.timestampMs = 25;
    QVERIFY(worker.enqueueFrame(frameData));

    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() == 1, 3000);
    QVERIFY(!worker.isRunning());

    worker.requestFinish();
    QTest::qWait(200);

    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(encoder->finishCallCount(), 0);
}

void TestEncodingWorker::testStopDuringFrameProcessingResetsProcessingState()
{
    EncodingWorker worker;
    auto* encoder = new BlockingVideoEncoder();
    worker.setEncoderType(EncodingWorker::EncoderType::Video);
    worker.setVideoEncoder(encoder);
    QVERIFY(worker.start());

    EncodingWorker::FrameData frameData;
    frameData.frame = createTestFrame();
    frameData.timestampMs = 42;
    QVERIFY(worker.enqueueFrame(frameData));
    QVERIFY(encoder->waitUntilWriteEntered(3000));

    const QFuture<void> stopFuture = QtConcurrent::run([&worker]() {
        worker.stop();
    });

    encoder->releaseWrite();
    QTRY_VERIFY_WITH_TIMEOUT(stopFuture.isFinished(), 3000);
    QVERIFY(!worker.isRunning());
    QVERIFY(!worker.isProcessing());

    // Verify worker can be restarted and process new frames.
    QVERIFY(worker.start());

    frameData.timestampMs = 84;
    QVERIFY(worker.enqueueFrame(frameData));
    QTRY_COMPARE_WITH_TIMEOUT(encoder->writeCount(), 2, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!worker.isProcessing(), 3000);
}

QTEST_MAIN(TestEncodingWorker)
#include "tst_EncodingWorker.moc"
