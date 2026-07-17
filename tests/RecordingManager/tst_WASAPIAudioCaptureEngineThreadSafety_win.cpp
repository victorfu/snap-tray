#include <QtTest/QtTest>

#ifdef Q_OS_WIN

#include "capture/WASAPIAudioCaptureEngine.h"
#include "utils/ResourceCleanupHelper.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSemaphore>
#include <QThread>

#include <memory>

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
    void disposalWaitsForInFlightDirectCallback();
    void stopPreservesConnectionsForRestart();
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

void TestWASAPIAudioCaptureEngineThreadSafetyWin::disposalWaitsForInFlightDirectCallback()
{
    auto *engine = new WASAPIAudioCaptureEngine;
    QObject receiver;
    QSemaphore callbackEntered;
    QSemaphore releaseCallback;
    QSemaphore disposalFinished;

    QObject::connect(engine, &IAudioCaptureEngine::audioDataReady,
                     &receiver, [&callbackEntered, &releaseCallback](const QByteArray &, qint64) {
        callbackEntered.release();
        releaseCallback.acquire();
    }, Qt::DirectConnection);
    engine->enableDataCallbacks();

    QThread *emitterThread = QThread::create([engine]() {
        engine->deliverAudioData(QByteArrayLiteral("audio"), 0);
    });
    emitterThread->start();
    QVERIFY2(callbackEntered.tryAcquire(1, 1000), "Direct callback did not start");

    QPointer<WASAPIAudioCaptureEngine> engineGuard(engine);
    auto owner = std::make_unique<AudioEnginePtr>(engine);
    QThread *disposalThread = QThread::create([ownerPtr = owner.get(),
                                               &disposalFinished]() {
        ownerPtr->reset();
        disposalFinished.release();
    });
    disposalThread->start();

    const auto callbackGateClosed = [engine]() {
        QMutexLocker locker(&engine->m_dataCallbackMutex);
        return !engine->m_acceptingDataCallbacks;
    };
    QTRY_VERIFY_WITH_TIMEOUT(callbackGateClosed(), 1000);

    QVERIFY2(!disposalFinished.tryAcquire(),
             "Disposal returned while a DirectConnection callback was still running");

    releaseCallback.release();
    QVERIFY2(emitterThread->wait(1000), "Emitter thread did not finish");
    QVERIFY2(disposalFinished.tryAcquire(1, 1000), "Disposal did not drain the callback");
    QVERIFY2(disposalThread->wait(1000), "Disposal thread did not finish");

    delete emitterThread;
    delete disposalThread;
    QTRY_VERIFY_WITH_TIMEOUT(engineGuard.isNull(), 2000);
}

void TestWASAPIAudioCaptureEngineThreadSafetyWin::stopPreservesConnectionsForRestart()
{
    WASAPIAudioCaptureEngine engine;
    int deliveryCount = 0;

    QObject::connect(&engine, &IAudioCaptureEngine::audioDataReady,
                     &engine, [&deliveryCount](const QByteArray &, qint64) {
        ++deliveryCount;
    }, Qt::DirectConnection);

    engine.enableDataCallbacks();
    engine.deliverAudioData(QByteArrayLiteral("first"), 0);
    QCOMPARE(deliveryCount, 1);

    engine.stop();
    engine.enableDataCallbacks();
    engine.deliverAudioData(QByteArrayLiteral("second"), 1);
    QCOMPARE(deliveryCount, 2);
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
