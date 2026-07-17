#include <QtTest/QtTest>

#include <QDir>
#include <QFile>

#include "IVideoEncoder.h"
#include "video/VideoTrimmer.h"

class BackpressureVideoEncoder final : public IVideoEncoder
{
public:
    explicit BackpressureVideoEncoder(int rejectedAttempts, QObject *parent = nullptr)
        : IVideoEncoder(parent)
        , m_rejectedAttempts(rejectedAttempts)
    {}

    bool isAvailable() const override { return true; }
    QString encoderName() const override { return QStringLiteral("Backpressure test encoder"); }
    bool start(const QString &, const QSize &, int) override { m_running = true; return true; }

    void writeFrame(const QImage &, qint64) override
    {
        ++m_attempts;
        if (m_attempts > m_rejectedAttempts) {
            ++m_framesWritten;
        }
    }

    void finish() override { m_running = false; }
    void abort() override { m_running = false; }
    bool isRunning() const override { return m_running; }
    QString lastError() const override { return {}; }
    qint64 framesWritten() const override { return m_framesWritten; }
    QString outputPath() const override { return {}; }

    int attempts() const { return m_attempts; }

private:
    int m_rejectedAttempts = 0;
    int m_attempts = 0;
    qint64 m_framesWritten = 0;
    bool m_running = true;
};

class TestVideoTrimmerSafety : public QObject
{
    Q_OBJECT

private slots:
    void nativeEncoderBackpressureRetriesSameFrame();
    void avFoundationSeekUsesLockedAffinityHandoff();
    void mediaFoundationPausedSeekKeepsFramePending();
};

void TestVideoTrimmerSafety::nativeEncoderBackpressureRetriesSameFrame()
{
    BackpressureVideoEncoder encoder(1);
    VideoTrimmer trimmer;
    trimmer.m_running = true;
    trimmer.m_totalFrames = 1;
    trimmer.m_videoEncoder = &encoder;

    QImage frame(8, 8, QImage::Format_RGB32);
    frame.fill(Qt::red);
    trimmer.submitFrame(frame, 123);

    QCOMPARE(encoder.attempts(), 1);
    QCOMPARE(encoder.framesWritten(), 0);
    QVERIFY(trimmer.m_waitingForEncoder);
    QCOMPARE(trimmer.m_pendingFrame, frame);
    QCOMPARE(trimmer.m_pendingTimestamp, 123);

    trimmer.retryPendingFrame();

    QCOMPARE(encoder.attempts(), 2);
    QCOMPARE(encoder.framesWritten(), 1);
    QVERIFY(!trimmer.m_waitingForEncoder);
    QVERIFY(trimmer.m_pendingFrame.isNull());
    QCOMPARE(trimmer.m_frameCount, 1);

    // Keep teardown independent from the normal asynchronous completion path.
    trimmer.m_running = false;
    trimmer.m_videoEncoder = nullptr;
}

void TestVideoTrimmerSafety::avFoundationSeekUsesLockedAffinityHandoff()
{
    QFile source(QDir(QStringLiteral(VIDEO_SOURCE_ROOT))
                     .filePath(QStringLiteral("AVFoundationPlayer_mac.mm")));
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
    const QByteArray content = source.readAll();

    const qsizetype seekBegin = content.indexOf("void AVFoundationPlayer::seek(qint64 positionMs)");
    const qsizetype seekEnd = content.indexOf("void AVFoundationPlayer::setVolume", seekBegin);
    QVERIFY(seekBegin >= 0);
    QVERIFY(seekEnd > seekBegin);
    const QByteArray seek = content.mid(seekBegin, seekEnd - seekBegin);

    const qsizetype completion = seek.indexOf("completionHandler:^(BOOL finished)");
    const qsizetype completionLock = seek.indexOf("QMutexLocker locker(&seekState->mutex)",
                                                   completion);
    const qsizetype playerLookup = seek.indexOf(
        "AVFoundationPlayer *player = seekState->player", completionLock);
    const qsizetype affinityDispatch = seek.indexOf("QMetaObject::invokeMethod(player",
                                                      playerLookup);
    const qsizetype queuedLock = seek.indexOf("QMutexLocker locker(&seekState->mutex)",
                                               affinityDispatch);
    const qsizetype queuedLookup = seek.indexOf("player = seekState->player", queuedLock);
    QVERIFY(completion >= 0);
    QVERIFY(completionLock > completion);
    QVERIFY(playerLookup > completionLock);
    QVERIFY(affinityDispatch > playerLookup);
    QVERIFY(queuedLock > affinityDispatch);
    QVERIFY(queuedLookup > queuedLock);
    QVERIFY(seek.contains("const auto seekState = m_seekState"));
    QVERIFY(seek.contains("Qt::QueuedConnection"));
    QCOMPARE(seek.count("seekState->player"), qsizetype(2));
    QVERIFY(!seek.contains("QPointer"));
    QVERIFY(!seek.contains("dispatch_get_main_queue"));
    QVERIFY(!seek.contains("weakPlayer"));

    QVERIFY(content.contains("struct AVFoundationSeekState"));
    QVERIFY(content.contains("QMutex mutex"));
    QVERIFY(content.contains("std::shared_ptr<AVFoundationSeekState> m_seekState"));

    const qsizetype destructorBegin = content.indexOf("AVFoundationPlayer::~AVFoundationPlayer()");
    const qsizetype destructorEnd = content.indexOf("bool AVFoundationPlayer::load", destructorBegin);
    QVERIFY(destructorBegin >= 0);
    QVERIFY(destructorEnd > destructorBegin);
    const QByteArray destructor = content.mid(destructorBegin, destructorEnd - destructorBegin);
    const qsizetype teardownLock = destructor.indexOf("QMutexLocker locker(&m_seekState->mutex)");
    const qsizetype clearPlayer = destructor.indexOf("m_seekState->player = nullptr", teardownLock);
    const qsizetype cleanupHelper = destructor.indexOf("[m_helper cleanup]", clearPlayer);
    QVERIFY(teardownLock >= 0);
    QVERIFY(clearPlayer > teardownLock);
    QVERIFY(cleanupHelper > clearPlayer);
}

void TestVideoTrimmerSafety::mediaFoundationPausedSeekKeepsFramePending()
{
    QFile source(QDir(QStringLiteral(VIDEO_SOURCE_ROOT))
                     .filePath(QStringLiteral("MediaFoundationPlayer_win.cpp")));
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
    const QByteArray content = source.readAll();

    const qsizetype runBegin = content.indexOf("void run() override");
    const qsizetype runEnd = content.indexOf("private:", runBegin);
    QVERIFY(runBegin >= 0);
    QVERIFY(runEnd > runBegin);
    const QByteArray run = content.mid(runBegin, runEnd - runBegin);

    const qsizetype pendingDeclaration = run.indexOf("bool framePendingAfterSeek = false");
    const qsizetype pendingSet = run.indexOf("framePendingAfterSeek = true", pendingDeclaration);
    const qsizetype pauseGate = run.indexOf("m_paused && !framePendingAfterSeek", pendingSet);
    const qsizetype readSample = run.indexOf("m_reader->ReadSample", pauseGate);
    const qsizetype deliveredGuard = run.indexOf("if (frameDelivered)", readSample);
    const qsizetype pendingClear = run.indexOf("framePendingAfterSeek = false", deliveredGuard);
    const qsizetype endOfStream = run.indexOf("MF_SOURCE_READERF_ENDOFSTREAM", readSample);
    const qsizetype endOfStreamPendingClear = run.indexOf(
        "framePendingAfterSeek = false", endOfStream);
    const qsizetype endOfStreamWait = run.indexOf(
        "waitingForSeekAfterEndOfStream = true", endOfStreamPendingClear);
    const qsizetype endOfStreamSignal = run.indexOf("emit endOfStream()", endOfStreamWait);
    const qsizetype endOfStreamBlockEnd = run.indexOf(
        "bool frameDelivered = false", endOfStreamSignal);

    QVERIFY(pendingDeclaration >= 0);
    QVERIFY(pendingSet > pendingDeclaration);
    QVERIFY(pauseGate > pendingSet);
    QVERIFY(readSample > pauseGate);
    QVERIFY(deliveredGuard > readSample);
    QVERIFY(pendingClear > deliveredGuard);
    QVERIFY(endOfStream > readSample);
    QVERIFY(endOfStreamPendingClear > endOfStream);
    QVERIFY(endOfStreamWait > endOfStreamPendingClear);
    QVERIFY(endOfStreamSignal > endOfStreamWait);
    QVERIFY(endOfStreamBlockEnd > endOfStreamSignal);
    const QByteArray endOfStreamBlock = run.mid(
        endOfStream, endOfStreamBlockEnd - endOfStream);
    QVERIFY(endOfStreamBlock.contains("continue;"));
    QVERIFY(!endOfStreamBlock.contains("break;"));
    QCOMPARE(run.count("framePendingAfterSeek = false"), qsizetype(3));
    QVERIFY(run.contains("MF_SOURCE_READERF_ERROR"));
}

QTEST_MAIN(TestVideoTrimmerSafety)
#include "tst_VideoTrimmerSafety.moc"
