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
    void avFoundationSeekUsesGuardedPlayer();
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

void TestVideoTrimmerSafety::avFoundationSeekUsesGuardedPlayer()
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

    QVERIFY(seek.contains("QPointer<AVFoundationPlayer> weakPlayer(this)"));
    QCOMPARE(seek.count("weakPlayer.data()"), qsizetype(1));
    QVERIFY(!seek.contains("AVFoundationPlayer *player = this"));
    QVERIFY(!seek.contains("[this"));

    const qsizetype completion = seek.indexOf("completionHandler:^(BOOL finished)");
    const qsizetype mainDispatch = seek.indexOf("dispatch_async(dispatch_get_main_queue()", completion);
    QVERIFY(completion >= 0);
    QVERIFY(mainDispatch > completion);
    const QByteArray outerCompletion = seek.mid(completion, mainDispatch - completion);
    QVERIFY(!outerCompletion.contains("weakPlayer.data()"));
    QVERIFY(!outerCompletion.contains("player->state()"));

    const QByteArray mainBlock = seek.mid(mainDispatch);
    QVERIFY(mainBlock.contains("weakPlayer.data()"));
    QVERIFY(mainBlock.contains("player->state()"));

    const qsizetype clearHelper = content.indexOf("m_helper.player = nullptr");
    const qsizetype cleanupHelper = content.indexOf("[m_helper cleanup]", clearHelper);
    QVERIFY(clearHelper >= 0);
    QVERIFY(cleanupHelper > clearHelper);
}

QTEST_MAIN(TestVideoTrimmerSafety)
#include "tst_VideoTrimmerSafety.moc"
