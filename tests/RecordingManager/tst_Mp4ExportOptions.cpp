#include <QtTest>
#include <cmath>

#include "encoding/VideoBitrate.h"
#include "recording/Mp4ExportOptions.h"
#include "video/IVideoTranscoder.h"

using namespace SnapTray;

class TestMp4ExportOptions : public QObject
{
    Q_OBJECT

private slots:
    void defaultsKeepTheExportAsBefore();
    void outputSizeKeepsAspectAndNeverUpscales_data();
    void outputSizeKeepsAspectAndNeverUpscales();
    void frameRateNeverAboveTheSource();
    void qualityAndCodecSetTheBitrate();
    void changesStream();
    void jsonRoundTrip();
    void estimate();
    void estimateFollowsTheRecording();
};

void TestMp4ExportOptions::defaultsKeepTheExportAsBefore()
{
    const Mp4ExportOptions options;
    QCOMPARE(options.outputSize(QSize(1920, 1080)), QSize(1920, 1080));
    QCOMPARE(options.outputFrameRate(59.94), 60);
    QCOMPARE(options.outputQuality(72), 72);
    QCOMPARE(options.codecId(), QString::fromLatin1(kVideoCodecH264));
    // The same bitrate the export used before #190.
    QCOMPARE(options.videoBitrate(QSize(1920, 1080), 30, 72), VideoBitrate::forQuality(QSize(1920, 1080), 30, 72));
    QVERIFY(!options.changesStream(QSize(1920, 1080), 30.0, QStringLiteral("h264")));
}

void TestMp4ExportOptions::outputSizeKeepsAspectAndNeverUpscales_data()
{
    QTest::addColumn<int>("resolution");
    QTest::addColumn<int>("customWidth");
    QTest::addColumn<QSize>("source");
    QTest::addColumn<QSize>("expected");
    const int p1080 = int(Mp4ExportOptions::Resolution::P1080);
    const int p720 = int(Mp4ExportOptions::Resolution::P720);
    const int custom = int(Mp4ExportOptions::Resolution::CustomWidth);
    QTest::newRow("4K to 1080p") << p1080 << 0 << QSize(3840, 2160) << QSize(1920, 1080);
    QTest::newRow("1440p to 720p") << p720 << 0 << QSize(2560, 1440) << QSize(1280, 720);
    QTest::newRow("portrait crop to 720p") << p720 << 0 << QSize(1080, 1920) << QSize(720, 1280);
    QTest::newRow("720p source stays at 1080p") << p1080 << 0 << QSize(1280, 720) << QSize(1280, 720);
    QTest::newRow("odd result made even") << p720 << 0 << QSize(1366, 768) << QSize(1280, 720);
    QTest::newRow("odd crop") << p720 << 0 << QSize(1002, 1000) << QSize(720, 720);
    QTest::newRow("custom width") << custom << 1000 << QSize(1920, 1080) << QSize(1000, 562);
    QTest::newRow("custom wider than source") << custom << 4000 << QSize(1920, 1080) << QSize(1920, 1080);
    QTest::newRow("custom below minimum") << custom << 10 << QSize(1920, 1080)
                                          << QSize(Mp4ExportOptions::kMinCustomWidth, 36);
}

void TestMp4ExportOptions::outputSizeKeepsAspectAndNeverUpscales()
{
    QFETCH(int, resolution);
    QFETCH(int, customWidth);
    QFETCH(QSize, source);
    QFETCH(QSize, expected);
    Mp4ExportOptions options;
    options.resolution = Mp4ExportOptions::Resolution(resolution);
    if (customWidth > 0) options.customWidth = customWidth;
    const QSize out = options.outputSize(source);
    QCOMPARE(out, expected);
    QCOMPARE(out.width() % 2, 0);
    QCOMPARE(out.height() % 2, 0);
    QVERIFY(out.width() <= source.width() && out.height() <= source.height());
}

void TestMp4ExportOptions::frameRateNeverAboveTheSource()
{
    Mp4ExportOptions options;
    options.frameRate = 30;
    QCOMPARE(options.outputFrameRate(60.0), 30);
    QCOMPARE(options.outputFrameRate(24.0), 24);
    options.frameRate = 15;
    QCOMPARE(options.outputFrameRate(29.97), 15);
    options.frameRate = 0;
    QCOMPARE(options.outputFrameRate(0.0), 30); // unknown source: the fallback rate
}

void TestMp4ExportOptions::qualityAndCodecSetTheBitrate()
{
    Mp4ExportOptions options;
    const QSize size(1920, 1080);
    options.quality = Mp4ExportOptions::Quality::Low;
    const int low = options.videoBitrate(size, 30, 80);
    options.quality = Mp4ExportOptions::Quality::High;
    const int high = options.videoBitrate(size, 30, 80);
    QVERIFY(low < high);
    QCOMPARE(options.outputQuality(80), Mp4ExportOptions::kQualityHigh);
    options.codec = Mp4ExportOptions::Codec::Hevc;
    QCOMPARE(options.codecId(), QString::fromLatin1(kVideoCodecHevc));
    const int hevc = options.videoBitrate(size, 30, 80);
    QVERIFY(hevc < high);
    QVERIFY(hevc >= VideoBitrate::kMinBitrate);
}

void TestMp4ExportOptions::changesStream()
{
    const QSize source(1920, 1080);
    Mp4ExportOptions options;
    options.quality = Mp4ExportOptions::Quality::High; // quality alone is the smart-save rule's business
    QVERIFY(!options.changesStream(source, 30.0, QStringLiteral("h264")));
    options.resolution = Mp4ExportOptions::Resolution::P1080; // already 1080p: nothing changes
    QVERIFY(!options.changesStream(source, 30.0, QStringLiteral("h264")));
    options.resolution = Mp4ExportOptions::Resolution::P720;
    QVERIFY(options.changesStream(source, 30.0, QStringLiteral("h264")));
    options.resolution = Mp4ExportOptions::Resolution::Original;
    options.frameRate = 60; // not above the source's 30
    QVERIFY(!options.changesStream(source, 30.0, QStringLiteral("h264")));
    options.frameRate = 15;
    QVERIFY(options.changesStream(source, 30.0, QStringLiteral("h264")));
    options.frameRate = 0;
    options.codec = Mp4ExportOptions::Codec::Hevc;
    QVERIFY(options.changesStream(source, 30.0, QStringLiteral("h264")));
    QVERIFY(!options.changesStream(source, 30.0, QStringLiteral("hevc")));
}

void TestMp4ExportOptions::jsonRoundTrip()
{
    Mp4ExportOptions options;
    options.resolution = Mp4ExportOptions::Resolution::CustomWidth;
    options.customWidth = 1000;
    options.frameRate = 15;
    options.quality = Mp4ExportOptions::Quality::Medium;
    options.codec = Mp4ExportOptions::Codec::Hevc;
    QCOMPARE(Mp4ExportOptions::fromJson(options.toJson()), options);
    // Unknown or missing values keep the defaults.
    const auto odd = Mp4ExportOptions::fromJson({{QStringLiteral("resolution"), QStringLiteral("8k")},
                                                 {QStringLiteral("frameRate"), 47},
                                                 {QStringLiteral("codec"), QStringLiteral("av1")},
                                                 {QStringLiteral("customWidth"), 1}});
    QCOMPARE(odd.resolution, Mp4ExportOptions::Resolution::Original);
    QCOMPARE(odd.frameRate, 0);
    QCOMPARE(odd.codec, Mp4ExportOptions::Codec::H264);
    QCOMPARE(odd.customWidth, Mp4ExportOptions::kMinCustomWidth);
    QCOMPARE(Mp4ExportOptions::fromJson({}), Mp4ExportOptions());
}

void TestMp4ExportOptions::estimate()
{
    // 4 Mbit/s for 10 s is 5 MB, plus audio and container overhead.
    const qint64 videoOnly = Mp4SizeEstimate::bytes(4000000, false, 10000);
    QCOMPARE(videoOnly, qint64(5000000 * Mp4SizeEstimate::kContainerOverhead));
    const qint64 withAudio = Mp4SizeEstimate::bytes(4000000, true, 10000);
    QCOMPARE(withAudio - videoOnly,
             qint64(Mp4SizeEstimate::kAudioBitrate / 8 * 10 * Mp4SizeEstimate::kContainerOverhead));
    QCOMPARE(Mp4SizeEstimate::bytes(4000000, true, 0), 0);
}

// A still recording needs far less than the target; noise reaches it.
void TestMp4ExportOptions::estimateFollowsTheRecording()
{
    using namespace Mp4SizeEstimate;
    const QSize size(1920, 1080);
    constexpr int kTarget = 8000000;
    // 1 Mbit/s of video for 10 s, with 192 kbit/s audio and container overhead.
    Source still;
    still.durationMs = 10000;
    still.hasAudio = true;
    still.videoSize = size;
    still.fileBytes = qint64((1000000 + kAudioBitrate) / 8 * 10 * kContainerOverhead);
    QCOMPARE(expectedVideoBitrate(kTarget, false, still, size), 1000000);
    // Half the area (a crop or 0.707x scale) needs about half.
    QCOMPARE(expectedVideoBitrate(kTarget, false, still, QSize(960, 1080)), 500000);
    // HEVC needs less for the same look, H.264 from an HEVC recording more.
    QCOMPARE(expectedVideoBitrate(kTarget, true, still, size),
             int(std::ceil(1000000 * Mp4ExportOptions::kHevcBitrateFactor)));
    Source hevc = still;
    hevc.hevc = true;
    QCOMPARE(expectedVideoBitrate(kTarget, false, hevc, size),
             int(std::ceil(1000000 / Mp4ExportOptions::kHevcBitrateFactor)));

    // Never above the target: the encoder will not write more.
    Source noise = still;
    noise.fileBytes *= 40;
    QCOMPARE(expectedVideoBitrate(kTarget, false, noise, size), kTarget);

    // Nothing to go on: the target.
    QCOMPARE(expectedVideoBitrate(kTarget, false, Source(), size), kTarget);
    Source noLength = still;
    noLength.durationMs = 0;
    QCOMPARE(expectedVideoBitrate(kTarget, false, noLength, size), kTarget);
}

QTEST_MAIN(TestMp4ExportOptions)
#include "tst_Mp4ExportOptions.moc"
