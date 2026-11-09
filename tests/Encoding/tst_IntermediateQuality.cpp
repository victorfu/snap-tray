#include "encoding/IntermediateQuality.h"
#include "encoding/VideoBitrate.h"
#include "video/IVideoTranscoder.h"

#include <QtTest>

using namespace SnapTray;

class tst_IntermediateQuality : public QObject
{
    Q_OBJECT
private slots:
    void intermediateBitrate_data();
    void intermediateBitrate();
    void bytesPerMinuteAndRoom();
    void averageBitrate_data();
    void averageBitrate();
    void effectiveFrameRate_data();
    void effectiveFrameRate();
    void outputBitrateUsesClampedQualityAndFallbackFps();
    void smartSaveShouldMove_data();
    void smartSaveShouldMove();
};

void tst_IntermediateQuality::intermediateBitrate_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<int>("fps");
    QTest::addColumn<int>("expected");
    // 1920*1080*30*0.55 = 34,214,400
    QTest::newRow("1080p30") << QSize(1920, 1080) << 30 << 34214400;
    // 5120*2880*60*0.55 = 486,604,800 -> capped
    QTest::newRow("5k60 caps at 100 Mbps") << QSize(5120, 2880) << 60 << IntermediateQuality::kIntermediateMaxBitrate;
    // 64*48*10*0.55 = 16,896 -> floored to kMinBitrate
    QTest::newRow("tiny floors") << QSize(64, 48) << 10 << VideoBitrate::kMinBitrate;
}

void tst_IntermediateQuality::intermediateBitrate()
{
    QFETCH(QSize, size);
    QFETCH(int, fps);
    QFETCH(int, expected);
    QCOMPARE(IntermediateQuality::intermediateBitrate(size, fps), expected);
}

void tst_IntermediateQuality::bytesPerMinuteAndRoom()
{
    // 34,214,400 bps = 4,276,800 B/s = 256,608,000 B/min
    QCOMPARE(IntermediateQuality::estimatedBytesPerMinute(34214400), qint64(256608000));
    const QSize size(1920, 1080);
    const qint64 needed = qint64(256608000) * IntermediateQuality::kMinimumRecordingMinutes;
    QVERIFY(IntermediateQuality::hasRoomForIntermediate(needed, size, 30));
    QVERIFY(!IntermediateQuality::hasRoomForIntermediate(needed - 1, size, 30));
    QVERIFY(!IntermediateQuality::hasRoomForIntermediate(0, size, 30));
    QVERIFY(!IntermediateQuality::hasRoomForIntermediate(-1, size, 30));
}

void tst_IntermediateQuality::averageBitrate_data()
{
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<qint64>("durationMs");
    QTest::addColumn<qint64>("expected");
    QTest::newRow("1 MB over 1 s") << qint64(1000000) << qint64(1000) << qint64(8000000);
    QTest::newRow("250 KB over 2 s") << qint64(250000) << qint64(2000) << qint64(1000000);
    QTest::newRow("zero bytes") << qint64(0) << qint64(2000) << qint64(0);
    QTest::newRow("zero duration") << qint64(250000) << qint64(0) << qint64(0);
    QTest::newRow("negative duration") << qint64(250000) << qint64(-5) << qint64(0);
}

void tst_IntermediateQuality::averageBitrate()
{
    QFETCH(qint64, bytes);
    QFETCH(qint64, durationMs);
    QFETCH(qint64, expected);
    QCOMPARE(IntermediateQuality::averageBitrate(bytes, durationMs), expected);
}

void tst_IntermediateQuality::effectiveFrameRate_data()
{
    QTest::addColumn<double>("probed");
    QTest::addColumn<int>("expected");
    QTest::newRow("exact 30") << 30.0 << 30;
    QTest::newRow("29.97 rounds") << 29.97 << 30;
    QTest::newRow("10.4 rounds down") << 10.4 << 10;
    QTest::newRow("zero falls back") << 0.0 << IntermediateQuality::kFallbackFrameRate;
    QTest::newRow("negative falls back") << -1.0 << IntermediateQuality::kFallbackFrameRate;
    QTest::newRow("below half rounds to one") << 0.4 << IntermediateQuality::kFallbackFrameRate;
}

void tst_IntermediateQuality::effectiveFrameRate()
{
    QFETCH(double, probed);
    QFETCH(int, expected);
    QCOMPARE(IntermediateQuality::effectiveFrameRate(probed), expected);
}

void tst_IntermediateQuality::outputBitrateUsesClampedQualityAndFallbackFps()
{
    const QSize size(1280, 720);
    QCOMPARE(IntermediateQuality::outputBitrateFor(size, 30.0, 55), VideoBitrate::forQuality(size, 30, 55));
    QCOMPARE(IntermediateQuality::outputBitrateFor(size, 0.0, 55),
             VideoBitrate::forQuality(size, IntermediateQuality::kFallbackFrameRate, 55));
    // The settings manager does not clamp; the formula does.
    QCOMPARE(IntermediateQuality::outputBitrateFor(size, 30.0, 150), VideoBitrate::forQuality(size, 30, 100));
    QCOMPARE(IntermediateQuality::outputBitrateFor(size, 30.0, -3), VideoBitrate::forQuality(size, 30, 0));
}

void tst_IntermediateQuality::smartSaveShouldMove_data()
{
    QTest::addColumn<bool>("valid");
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QSize>("size");
    QTest::addColumn<double>("fps");
    QTest::addColumn<qint64>("durationMs");
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<int>("quality");
    QTest::addColumn<bool>("expected");

    // 1280x720 @ 10 fps, quality 100 -> 2,764,800 bps target; quality 0 -> 921,600 -> floored to 1,000,000.
    const QSize hd(1280, 720);
    QTest::newRow("at target moves") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(691200) << 100 << true; // 2,764,800 bps
    QTest::newRow("one byte over re-encodes") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(691201) << 100 << false;
    QTest::newRow("intermediate vs low quality re-encodes") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(691200) << 0 << false;
    QTest::newRow("static recording under floor moves") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(20000) << 0 << true; // 80 kbps
    QTest::newRow("hevc never moves") << true << "hevc" << hd << 10.0 << qint64(2000) << qint64(20000) << 100 << false;
    QTest::newRow("unknown codec never moves") << true << "" << hd << 10.0 << qint64(2000) << qint64(20000) << 100 << false;
    QTest::newRow("invalid probe re-encodes") << false << "h264" << hd << 10.0 << qint64(2000) << qint64(20000) << 100 << false;
    QTest::newRow("zero bytes re-encodes") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(0) << 100 << false;
    QTest::newRow("unknown fps uses fallback 30") << true << "h264" << hd << 0.0 << qint64(2000) << qint64(2073600) << 100 << true; // 8,294,400 bps == forQuality(hd, 30, 100)
}

void tst_IntermediateQuality::smartSaveShouldMove()
{
    QFETCH(bool, valid);
    QFETCH(QString, codec);
    QFETCH(QSize, size);
    QFETCH(double, fps);
    QFETCH(qint64, durationMs);
    QFETCH(qint64, bytes);
    QFETCH(int, quality);
    QFETCH(bool, expected);
    VideoFileProbe probe;
    probe.valid = valid;
    probe.videoCodec = codec;
    probe.videoSize = size;
    probe.frameRate = fps;
    probe.durationMs = durationMs;
    QCOMPARE(IntermediateQuality::smartSaveShouldMove(probe, bytes, quality), expected);
}

QTEST_APPLESS_MAIN(tst_IntermediateQuality)
#include "tst_IntermediateQuality.moc"
