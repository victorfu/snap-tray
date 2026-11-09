#include <QtTest>
#include <QSemaphore>
#include <QThread>
#include <memory>
#include "video/SourceReaderMailbox.h"
#ifdef Q_OS_WIN
#include "video/MediaFoundationReaderCallback_win.h"
#endif

class tst_SourceReaderMailbox : public QObject
{
    Q_OBJECT
private slots:
    void cancelWithoutDecoderResponse();
    void deliveryAndLateCallbackLifetime();
    void concurrentCancellation();
    void nativeCallback();
};

void tst_SourceReaderMailbox::cancelWithoutDecoderResponse()
{
    SourceReaderMailbox<int> mailbox;
    QSemaphore started;
    bool cancelled = false;
    std::unique_ptr<QThread> worker(QThread::create([&] {
        started.release();
        cancelled = !mailbox.wait().has_value();
    }));
    worker->start();
    const bool workerStarted = started.tryAcquire(1, 1000);
    // Exercise the event loop while the decoder wait is outstanding. A 1 ms
    // timer in a fixed 30 ms window depends on macOS/CI timer scheduling and
    // does not measure whether the mailbox blocks the GUI thread.
    bool cancellationDispatched = false;
    QObject receiver;
    QMetaObject::invokeMethod(&receiver, [&] {
        cancellationDispatched = true;
        mailbox.cancel();
    }, Qt::QueuedConnection);
    const bool stayedResponsive = QTest::qWaitFor([&] { return cancellationDispatched; }, 1000);
    // Always release the worker, including when an assertion will fail.
    mailbox.cancel();
    QVERIFY(worker->wait(1000));
    QVERIFY(workerStarted);
    QVERIFY(cancelled);
    QVERIFY(stayedResponsive);
}

void tst_SourceReaderMailbox::deliveryAndLateCallbackLifetime()
{
    SourceReaderMailbox<std::shared_ptr<int>> mailbox;
    auto sample = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime = sample;
    mailbox.deliver(sample);
    sample.reset();
    auto result = mailbox.wait();
    QVERIFY(result.has_value());
    QCOMPARE(**result, 42);
    QVERIFY(!lifetime.expired());
    result.reset();
    QVERIFY(lifetime.expired());

    sample = std::make_shared<int>(7);
    lifetime = sample;
    mailbox.deliver(sample);
    sample.reset();
    mailbox.cancel();
    QVERIFY(lifetime.expired());
    sample = std::make_shared<int>(8);
    lifetime = sample;
    mailbox.deliver(std::move(sample)); // Late native callback owns no UI state.
    QVERIFY(lifetime.expired());
    QVERIFY(!mailbox.wait().has_value());
}

void tst_SourceReaderMailbox::concurrentCancellation()
{
    for (int iteration = 0; iteration < 500; ++iteration) {
        SourceReaderMailbox<std::shared_ptr<int>> mailbox;
        auto sample = std::make_shared<int>(iteration);
        std::weak_ptr<int> lifetime = sample;
        std::unique_ptr<QThread> callback(QThread::create([&] {
            mailbox.deliver(std::move(sample));
        }));
        callback->start();
        mailbox.cancel();
        QVERIFY(callback->wait(1000));
        QVERIFY(lifetime.expired());
        QVERIFY(!mailbox.wait().has_value());
    }
}

void tst_SourceReaderMailbox::nativeCallback()
{
#ifdef Q_OS_WIN
    auto* callback = new MediaFoundationReaderCallback;
    QCOMPARE(callback->OnReadSample(S_OK, 0, 2, 1234, nullptr), S_OK);
    const auto result = callback->wait();
    QVERIFY(result.has_value());
    QCOMPARE(result->flags, DWORD(2));
    QCOMPARE(result->timestamp, LONGLONG(1234));
    QVERIFY(!result->sample);
    callback->cancel();
    QCOMPARE(callback->OnReadSample(E_FAIL, 0, 0, 0, nullptr), S_OK);
    QVERIFY(!callback->wait().has_value());
    QCOMPARE(callback->Release(), ULONG(0));
#else
    QSKIP("Windows COM callback test only");
#endif
}

QTEST_GUILESS_MAIN(tst_SourceReaderMailbox)
#include "tst_SourceReaderMailbox.moc"
