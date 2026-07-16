#include <QtTest/QtTest>

#ifdef Q_OS_WIN

#include "capture/WASAPIAudioCaptureEngine.h"
#include "utils/ResourceCleanupHelper.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSemaphore>
#include <QThread>

namespace {

class BlockingThread final : public QThread
{
public:
    QSemaphore entered;
    QSemaphore release;

protected:
    void run() override
    {
        entered.release();
        release.acquire();
    }
};

} // namespace

class TestWASAPIAudioCaptureEngineThreadSafetyWin : public QObject
{
    Q_OBJECT

private slots:
    void runningThreadIsRetainedUntilItFinishes();
    void disposalReturnsWithoutWaitingForRunningThread();
    void disposalCleansUpAlreadyFinishedThread();
};

void TestWASAPIAudioCaptureEngineThreadSafetyWin::runningThreadIsRetainedUntilItFinishes()
{
    WASAPIAudioCaptureEngine engine;
    auto *thread = new BlockingThread;
    engine.m_captureThread = thread;
    thread->start();

    QVERIFY2(thread->entered.tryAcquire(1, 1000), "Test thread did not start");
    QVERIFY(!engine.releaseCaptureThreadIfFinished());
    QCOMPARE(engine.m_captureThread, static_cast<QThread *>(thread));
    QVERIFY(thread->isRunning());

    thread->release.release();
    QVERIFY2(thread->wait(1000), "Test thread did not stop");
    QVERIFY(engine.releaseCaptureThreadIfFinished());
    QVERIFY(engine.m_captureThread == nullptr);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::disposalReturnsWithoutWaitingForRunningThread()
{
    auto *engine = new WASAPIAudioCaptureEngine;
    auto *thread = new BlockingThread;
    engine->m_captureThread = thread;
    QObject::connect(thread, &QThread::finished,
                     engine, &WASAPIAudioCaptureEngine::onCaptureThreadFinished,
                     Qt::QueuedConnection);
    thread->start();
    QVERIFY2(thread->entered.tryAcquire(1, 1000), "Test thread did not start");

    QPointer<WASAPIAudioCaptureEngine> engineGuard(engine);
    QPointer<QThread> threadGuard(thread);
    AudioEnginePtr owner(engine);

    QElapsedTimer elapsed;
    elapsed.start();
    owner.reset();
    QVERIFY2(elapsed.elapsed() < 250, "Async disposal blocked on the running thread");
    QVERIFY(!engineGuard.isNull());
    QVERIFY(threadGuard && threadGuard->isRunning());

    thread->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(threadGuard.isNull(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(engineGuard.isNull(), 2000);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::disposalCleansUpAlreadyFinishedThread()
{
    auto *engine = new WASAPIAudioCaptureEngine;
    auto *thread = new BlockingThread;
    engine->m_captureThread = thread;
    QObject::connect(thread, &QThread::finished,
                     engine, &WASAPIAudioCaptureEngine::onCaptureThreadFinished,
                     Qt::QueuedConnection);
    thread->start();
    QVERIFY2(thread->entered.tryAcquire(1, 1000), "Test thread did not start");
    thread->release.release();
    QVERIFY2(thread->wait(1000), "Test thread did not stop");

    QPointer<WASAPIAudioCaptureEngine> engineGuard(engine);
    QPointer<QThread> threadGuard(thread);
    AudioEnginePtr owner(engine);
    owner.reset();

    QTRY_VERIFY_WITH_TIMEOUT(threadGuard.isNull(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(engineGuard.isNull(), 2000);
}

QTEST_GUILESS_MAIN(TestWASAPIAudioCaptureEngineThreadSafetyWin)
#include "tst_WASAPIAudioCaptureEngineThreadSafety_win.moc"

#else

class TestWASAPIAudioCaptureEngineThreadSafetyWin : public QObject
{
    Q_OBJECT
};

QTEST_GUILESS_MAIN(TestWASAPIAudioCaptureEngineThreadSafetyWin)
#include "tst_WASAPIAudioCaptureEngineThreadSafety_win.moc"

#endif // Q_OS_WIN
