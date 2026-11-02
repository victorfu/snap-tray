#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QtConcurrent>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include "encoding/EncodingWorker.h"
#include "IVideoEncoder.h"
#include "recording/CapturedFrame.h"
#include "recording/RecordingHealth.h"

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

    void writeAudioSamples(const QByteArray& pcmData, qint64 startFrame) override
    {
        Q_UNUSED(pcmData);
        if (m_throwOnAudio) {
            throw std::runtime_error("mock audio write failure");
        }
        m_lastAudioStartFrame = startFrame;
    }

    qint64 lastAudioStartFrame() const { return m_lastAudioStartFrame.load(); }

private:
    const bool m_throwOnFrame;
    const bool m_throwOnAudio;
    bool m_running = true;
    qint64 m_framesWritten = 0;
    std::atomic<int> m_finishCalls{0};
    std::atomic<qint64> m_lastAudioStartFrame{-1};
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

    bool writeEntered() const
    {
        QMutexLocker locker(&m_mutex);
        return m_writeEntered;
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

class SlowFinishVideoEncoder final : public IVideoEncoder
{
public:
    explicit SlowFinishVideoEncoder(int finishDelayMs, QObject* parent = nullptr)
        : IVideoEncoder(parent)
        , m_finishDelayMs(finishDelayMs)
    {
    }

    bool isAvailable() const override { return true; }
    QString encoderName() const override { return QStringLiteral("SlowFinishVideoEncoder"); }

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
        ++m_framesWritten;
    }

    void finish() override
    {
        ++m_finishCalls;
        m_finishThreadId.store(reinterpret_cast<quintptr>(QThread::currentThreadId()));
        QThread::msleep(static_cast<unsigned long>(m_finishDelayMs));
        m_running = false;
        emit finished(true, m_outputPath);
    }

    void abort() override
    {
        m_running = false;
    }

    bool isRunning() const override { return m_running; }
    QString lastError() const override { return QString(); }
    qint64 framesWritten() const override { return m_framesWritten.load(); }
    QString outputPath() const override { return m_outputPath; }
    int finishCallCount() const { return m_finishCalls.load(); }
    quintptr finishThreadId() const { return m_finishThreadId.load(); }

private:
    const int m_finishDelayMs;
    bool m_running = true;
    std::atomic<qint64> m_framesWritten{0};
    std::atomic<int> m_finishCalls{0};
    std::atomic<quintptr> m_finishThreadId{0};
    QString m_outputPath;
};

// Takes `delayMs` per frame and, optionally, rejects every other frame, like
// a busy hardware encoder. Frame counting is synchronous, as in the real ones.
class SlowRejectingVideoEncoder final : public IVideoEncoder
{
public:
    SlowRejectingVideoEncoder(int delayMs, bool rejectOdd) : m_delayMs(delayMs), m_rejectOdd(rejectOdd) {}
    bool isAvailable() const override { return true; }
    QString encoderName() const override { return QStringLiteral("SlowRejectingVideoEncoder"); }
    bool start(const QString& outputPath, const QSize&, int) override { m_outputPath = outputPath; m_running = true; return true; }
    void writeFrame(const QImage&, qint64) override
    {
        QThread::msleep(m_delayMs);
        if (m_rejectOdd && (m_calls++ % 2 == 1)) return;
        ++m_framesWritten;
    }
    void writeAudioSamples(const QByteArray&, qint64) override { ++m_audioPackets; }
    void finish() override { m_running = false; emit finished(true, m_outputPath); }
    void abort() override { m_running = false; }
    bool isRunning() const override { return m_running; }
    QString lastError() const override { return {}; }
    qint64 framesWritten() const override { return m_framesWritten; }
    QString outputPath() const override { return m_outputPath; }
    std::atomic<int> m_audioPackets{0};

private:
    int m_delayMs;
    bool m_rejectOdd;
    int m_calls = 0;
    std::atomic<qint64> m_framesWritten{0};
    bool m_running = false;
    QString m_outputPath;
};

// Runs a worker on its own thread and joins it, as RecordingManager does.
struct ThreadedWorker {
    EncodingWorker worker;
    QThread thread;
    explicit ThreadedWorker(IVideoEncoder* encoder)
    {
        worker.setEncoderType(EncodingWorker::EncoderType::Video);
        worker.setVideoEncoder(encoder);
        worker.moveToThread(&thread);
        thread.start();
    }
    ~ThreadedWorker()
    {
        QThread* mainThread = QCoreApplication::instance()->thread();
        QMetaObject::invokeMethod(&worker, [this, mainThread]() { worker.moveToThread(mainThread); },
                                  Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait(5000);
    }
};

class TestEncodingWorker : public QObject
{
    Q_OBJECT

private slots:
    void testFrameExceptionStopsWorker();
    void testAudioExceptionStopsWorker();
    void testAudioFrameTimestampPreservesPrecision();
    void testRequestFinishAfterFailureDoesNotEmitFinished();
    void testRequestFinishIsAsyncAndRunsOnWorkerThread();
    void testStopDuringFrameProcessingResetsProcessingState();
    void testFullQueueDropsNewFramesWithoutBlocking();
    void testByteBudgetBoundsRetainedFrames();
    void testRestartClearsQueuedBytes();
    void testStaleQueueDropsAndAges();
    void testEncoderRejectionsAreCounted();
    void testAudioOverloadIsCounted();
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

void TestEncodingWorker::testAudioFrameTimestampPreservesPrecision()
{
    EncodingWorker worker;
    auto* encoder = new ThrowingVideoEncoder(false, false);
    worker.setEncoderType(EncodingWorker::EncoderType::Video);
    worker.setVideoEncoder(encoder);
    QVERIFY(worker.start());

    constexpr qint64 startFrame = 512;
    worker.writeAudioSamples(QByteArray(128, char(0x7f)), startFrame);

    QTRY_COMPARE_WITH_TIMEOUT(encoder->lastAudioStartFrame(), startFrame, 3000);
    worker.stop();
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

void TestEncodingWorker::testRequestFinishIsAsyncAndRunsOnWorkerThread()
{
    EncodingWorker worker;
    auto* encoder = new SlowFinishVideoEncoder(200);
    worker.setEncoderType(EncodingWorker::EncoderType::Video);
    worker.setVideoEncoder(encoder);

    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    QVERIFY(worker.start());

    std::atomic<quintptr> workerThreadId{0};
    const bool capturedThreadId = QMetaObject::invokeMethod(&worker, [&workerThreadId]() {
        workerThreadId.store(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    }, Qt::BlockingQueuedConnection);
    QVERIFY(capturedThreadId);
    QVERIFY(workerThreadId.load() != 0);

    QSignalSpy finishedSpy(&worker, &EncodingWorker::finished);

    EncodingWorker::FrameData frameData;
    frameData.frame = createTestFrame();
    frameData.timestampMs = 12;
    QVERIFY(worker.enqueueFrame(frameData));

    QElapsedTimer timer;
    timer.start();
    worker.requestFinish();
    QVERIFY2(timer.elapsed() < 100, "requestFinish() blocked the caller thread");

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(encoder->finishCallCount(), 1);
    QCOMPARE(encoder->finishThreadId(), workerThreadId.load());
    QVERIFY(encoder->finishThreadId() != reinterpret_cast<quintptr>(QThread::currentThreadId()));

    QThread* mainThread = QCoreApplication::instance()->thread();
    const bool movedBack = QMetaObject::invokeMethod(&worker, [&worker, mainThread]() {
        worker.moveToThread(mainThread);
    }, Qt::BlockingQueuedConnection);
    QVERIFY(movedBack);

    workerThread.quit();
    QVERIFY(workerThread.wait(3000));
}

void TestEncodingWorker::testStopDuringFrameProcessingResetsProcessingState()
{
    EncodingWorker worker;
    auto* encoder = new BlockingVideoEncoder();
    worker.setEncoderType(EncodingWorker::EncoderType::Video);
    worker.setVideoEncoder(encoder);
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();
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

    QThread* mainThread = QCoreApplication::instance()->thread();
    const bool movedBack = QMetaObject::invokeMethod(&worker, [&worker, mainThread]() {
        worker.moveToThread(mainThread);
    }, Qt::BlockingQueuedConnection);
    QVERIFY(movedBack);

    workerThread.quit();
    QVERIFY(workerThread.wait(3000));
}

namespace {
QImage frameOfSize(int side)
{
    QImage frame(side, side, QImage::Format_ARGB32);
    frame.fill(Qt::blue);
    return frame;
}

bool finishAndWait(EncodingWorker& worker)
{
    QSignalSpy finished(&worker, &EncodingWorker::finished);
    worker.requestFinish();
    return finished.wait(20000) || finished.count() > 0;
}
} // namespace

void TestEncodingWorker::testFullQueueDropsNewFramesWithoutBlocking()
{
    ThreadedWorker run(new SlowRejectingVideoEncoder(20, false));
    auto health = std::make_shared<SnapTray::RecordingHealth>();
    health->reset(QStringLiteral("test"), 1);
    run.worker.setHealth(health);
    EncodingWorker::QueueBudget budget;
    budget.maxFrames = 8;
    run.worker.setQueueBudget(budget);
    QVERIFY(run.worker.start());

    // A source far faster than the encoder: the capture side never waits.
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 100; ++i) {
        run.worker.enqueueFrame({frameOfSize(16), i * 5});
        QVERIFY(run.worker.queueDepth() <= budget.maxFrames);
    }
    QVERIFY2(timer.elapsed() < 500, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(finishAndWait(run.worker));

    const auto s = health->snapshot(SnapTray::captureClockNowNs());
    QCOMPARE(s.offered, qint64(100));
    QVERIFY(s.droppedQueueFull > 0);
    // Every frame is accounted for, layer by layer.
    QCOMPARE(s.queued + s.droppedQueueFull + s.droppedQueueBytes + s.droppedQueueStale, s.offered);
    QCOMPARE(s.encoded + s.encoderRejected, s.queued);
    QVERIFY(s.peakQueueDepth <= budget.maxFrames);
    QVERIFY(s.queueAgeP95Ms >= s.queueAgeP50Ms);
    QVERIFY(s.encodeP50Ms >= 15);
    const QString text = SnapTray::RecordingHealth::formatDiagnostics(s);
    QVERIFY(text.contains(QStringLiteral("offered 100")));
}

void TestEncodingWorker::testByteBudgetBoundsRetainedFrames()
{
    ThreadedWorker run(new SlowRejectingVideoEncoder(20, false));
    auto health = std::make_shared<SnapTray::RecordingHealth>();
    run.worker.setHealth(health);
    const qint64 frameBytes = frameOfSize(64).sizeInBytes();  // 16 KiB
    EncodingWorker::QueueBudget budget;
    budget.maxFrames = 100;
    budget.maxBytes = 3 * frameBytes;
    run.worker.setQueueBudget(budget);
    QVERIFY(run.worker.start());
    for (int i = 0; i < 40; ++i) {
        run.worker.enqueueFrame({frameOfSize(64), i * 5});
    }
    QVERIFY(finishAndWait(run.worker));
    const auto s = health->snapshot(SnapTray::captureClockNowNs());
    QVERIFY(s.droppedQueueBytes > 0);
    QVERIFY2(s.peakQueueBytes <= budget.maxBytes, qPrintable(QString::number(s.peakQueueBytes)));
    QCOMPARE(s.droppedQueueFull, qint64(0));
}

void TestEncodingWorker::testRestartClearsQueuedBytes()
{
    // PR #170 review: stop() and start() cleared the frame queue but kept
    // its byte count, so a restarted worker dropped frames for a budget
    // that the discarded frames still seemed to use.
    EncodingWorker worker; // on this thread: nothing is processed until events run
    worker.setEncoderType(EncodingWorker::EncoderType::Video);
    worker.setVideoEncoder(new BlockingVideoEncoder());
    const qint64 frameBytes = frameOfSize(64).sizeInBytes();
    EncodingWorker::QueueBudget budget;
    budget.maxFrames = 100;
    budget.maxBytes = 3 * frameBytes;
    worker.setQueueBudget(budget);

    for (int run = 0; run < 2; ++run) {
        QVERIFY(worker.start());
        for (int i = 0; i < 3; ++i) {
            QVERIFY2(worker.enqueueFrame({frameOfSize(64), i * 5}),
                     qPrintable(QStringLiteral("run %1, frame %2").arg(run).arg(i)));
        }
        QCOMPARE(worker.queueDepth(), 3);
        worker.stop();
        QCOMPARE(worker.queueDepth(), 0);
    }
}

void TestEncodingWorker::testStaleQueueDropsAndAges()
{
    ThreadedWorker run(new SlowRejectingVideoEncoder(60, false));
    auto health = std::make_shared<SnapTray::RecordingHealth>();
    run.worker.setHealth(health);
    EncodingWorker::QueueBudget budget;
    budget.maxFrames = 100;
    budget.maxAgeMs = 50;
    run.worker.setQueueBudget(budget);
    QVERIFY(run.worker.start());
    for (int i = 0; i < 30; ++i) {
        run.worker.enqueueFrame({frameOfSize(16), i * 10});
        QThread::msleep(10);
    }
    QVERIFY(finishAndWait(run.worker));
    const auto s = health->snapshot(SnapTray::captureClockNowNs());
    // The encoder fell behind: new frames stop being queued instead of the
    // queue (and latency) growing without bound.
    QVERIFY(s.droppedQueueStale > 0);
    QVERIFY2(s.queueAgeP95Ms < 500, qPrintable(QString::number(s.queueAgeP95Ms)));
}

void TestEncodingWorker::testEncoderRejectionsAreCounted()
{
    ThreadedWorker run(new SlowRejectingVideoEncoder(1, true));
    auto health = std::make_shared<SnapTray::RecordingHealth>();
    run.worker.setHealth(health);
    QVERIFY(run.worker.start());
    for (int i = 0; i < 10; ++i) {
        QVERIFY(run.worker.enqueueFrame({frameOfSize(16), i * 33}));
        QTRY_COMPARE(run.worker.queueDepth(), 0);
    }
    QVERIFY(finishAndWait(run.worker));
    const auto s = health->snapshot(SnapTray::captureClockNowNs());
    QCOMPARE(s.queued, qint64(10));
    QCOMPARE(s.encoderRejected, qint64(5));
    QCOMPARE(s.encoded, qint64(5));
}

void TestEncodingWorker::testAudioOverloadIsCounted()
{
    auto* encoder = new SlowRejectingVideoEncoder(300, false);
    ThreadedWorker run(encoder);
    auto health = std::make_shared<SnapTray::RecordingHealth>();
    run.worker.setHealth(health);
    QVERIFY(run.worker.start());
    // The encoder is busy with a frame while audio keeps arriving.
    QVERIFY(run.worker.enqueueFrame({frameOfSize(16), 0}));
    QThread::msleep(50);
    for (int i = 0; i < 150; ++i) {
        run.worker.writeAudioSamples(QByteArray(64, '\0'), i * 16);
    }
    QVERIFY(finishAndWait(run.worker));
    const auto s = health->snapshot(SnapTray::captureClockNowNs());
    QVERIFY(s.audioDropped > 0);
    QCOMPARE(s.audioDropped + s.audioQueued, qint64(150));
    QCOMPARE(s.audioDroppedBytes, s.audioDropped * 64);
    QCOMPARE(qint64(encoder->m_audioPackets.load()), s.audioQueued);
}

QTEST_MAIN(TestEncodingWorker)
#include "tst_EncodingWorker.moc"
