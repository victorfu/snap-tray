#include <QtTest>

#include "encoding/VideoBitrate.h"
#include "recording/Mp4ExportOptions.h"
#include "video/Mp4VideoSamples.h"
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
    void samplesCountWhatARangeHolds();
    void samplesReadFromFragments();
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

// The estimate follows what the recording spent on the kept video, never
// above the target.
void TestMp4ExportOptions::estimateFollowsTheRecording()
{
    using namespace Mp4SizeEstimate;
    const QSize size(1920, 1080);
    constexpr int kTarget = 8000000;
    constexpr qint64 kTenSeconds = 10000;
    // What bytes() gives for so many video bits per second, with audio.
    const auto atBitrate = [](int videoBitsPerSecond) { return bytes(videoBitsPerSecond, true, kTenSeconds); };
    // The same for a bitrate that is not a whole number of bits.
    const auto atExactBitrate = [](double videoBitsPerSecond) {
        return qint64((videoBitsPerSecond + kAudioBitrate) * 10 / 8 * kContainerOverhead);
    };
    // A still part kept for 10 s: 1 Mbit/s of video in the recording.
    Source still;
    still.videoSize = size;
    still.keptVideoBytes = 1000000 / 8 * 10;
    QCOMPARE(expectedBytes(kTarget, false, still, size, true, kTenSeconds), atBitrate(1000000));
    // Half the area (a crop or 0.707x scale) needs about half.
    QCOMPARE(expectedBytes(kTarget, false, still, QSize(960, 1080), true, kTenSeconds), atBitrate(500000));
    // HEVC needs less for the same look, H.264 from an HEVC recording more.
    QCOMPARE(expectedBytes(kTarget, true, still, size, true, kTenSeconds),
             atExactBitrate(1000000 * Mp4ExportOptions::kHevcBitrateFactor));
    Source hevc = still;
    hevc.hevc = true;
    QCOMPARE(expectedBytes(kTarget, false, hevc, size, true, kTenSeconds),
             atExactBitrate(1000000 / Mp4ExportOptions::kHevcBitrateFactor));

    // Never above the target: the encoder will not write more.
    Source noise = still;
    noise.keptVideoBytes *= 40;
    QCOMPARE(expectedBytes(kTarget, false, noise, size, true, kTenSeconds), bytes(kTarget, true, kTenSeconds));

    // Nothing to go on: the target alone.
    QCOMPARE(expectedBytes(kTarget, false, Source(), size, true, kTenSeconds), bytes(kTarget, true, kTenSeconds));
    QCOMPARE(expectedBytes(kTarget, false, still, size, true, 0), 0);
}

// PR #213 review: what a time range holds is counted, not averaged. 590 still
// seconds then 10 busy ones: the busy part holds 60 Mbit, about 40x its share
// of the average, and the still part holds almost nothing.
void TestMp4ExportOptions::samplesCountWhatARangeHolds()
{
    Mp4VideoSamples samples;
    QVERIFY(!samples.isValid());
    QCOMPARE(samples.bytesBetween(0, 1000), 0);
    constexpr int kFps = 30;
    constexpr qint64 kStillFrame = 200;
    constexpr qint64 kBusyFrame = 6000000 / 8 / kFps;
    for (int i = 0; i < 600 * kFps; ++i) {
        samples.append(qint64(i) * 1000000 / kFps, i < 590 * kFps ? kStillFrame : kBusyFrame);
    }
    QVERIFY(samples.isValid());
    QCOMPARE(samples.sampleCount(), 600 * kFps);
    QCOMPARE(samples.bytesBetween(590000, 600000), 10 * kFps * kBusyFrame);
    QCOMPARE(samples.bytesBetween(0, 10000), 10 * kFps * kStillFrame);
    QCOMPARE(samples.bytesBetween(0, 600000), samples.totalBytes());
    // Half-open: a sample starting at the end is not counted.
    QCOMPARE(samples.bytesBetween(0, 1000), kFps * kStillFrame);
    QCOMPARE(samples.bytesBetween(5000, 5000), 0);
    QCOMPARE(samples.bytesBetween(700000, 800000), 0);
}

namespace {
QByteArray be32(quint32 v)
{
    QByteArray out(4, '\0');
    qToBigEndian(v, out.data());
    return out;
}
QByteArray be64(quint64 v)
{
    QByteArray out(8, '\0');
    qToBigEndian(v, out.data());
    return out;
}
QByteArray box(const char* type, const QByteArray& payload)
{
    return be32(quint32(8 + payload.size())) + QByteArray(type, 4) + payload;
}
QByteArray fullBox(const char* type, quint8 version, quint32 flags, const QByteArray& payload)
{
    return box(type, be32((quint32(version) << 24) | flags) + payload);
}
} // namespace

// A crash-safe recording as Media Foundation and FFmpeg write it: an empty
// sample table in moov, then fragments. The decode clock starts far from 0
// (tfdt), which must neither shift the times nor overflow them.
void TestMp4ExportOptions::samplesReadFromFragments()
{
    constexpr quint32 kTrack = 1;
    constexpr quint32 kTimescale = 90000;
    constexpr quint32 kFrame = kTimescale / 30;
    constexpr quint64 kClockStart = 1ull << 40; // about 140 days at 90 kHz
    const QByteArray tkhd = fullBox("tkhd", 0, 3, be32(0) + be32(0) + be32(kTrack) + QByteArray(68, '\0'));
    const QByteArray mdhd = fullBox("mdhd", 0, 0, be32(0) + be32(0) + be32(kTimescale) + be32(0) + be32(0));
    const QByteArray hdlr = fullBox("hdlr", 0, 0, be32(0) + QByteArray("vide") + QByteArray(13, '\0'));
    const QByteArray stbl = box("stbl", fullBox("stts", 0, 0, be32(0)) + fullBox("stsz", 0, 0, be32(0) + be32(0)));
    const QByteArray trak = box("trak", tkhd + box("mdia", mdhd + hdlr + box("minf", stbl)));
    // An audio track first: the video track must be found by its handler.
    const QByteArray audioTrak = box("trak", fullBox("tkhd", 0, 3, be32(0) + be32(0) + be32(2) + QByteArray(68, '\0'))
                                                 + box("mdia", mdhd + fullBox("hdlr", 0, 0, be32(0) + QByteArray("soun") + QByteArray(13, '\0'))));
    const QByteArray trex = fullBox("trex", 0, 0, be32(kTrack) + be32(1) + be32(kFrame) + be32(0) + be32(0));
    const QByteArray moov = box("moov", audioTrak + trak + box("mvex", trex));
    // Fragment 1: a 64-bit tfdt, sizes per sample, durations from trex.
    // Fragment 2: no tfdt (continues), version-0 header, a default size.
    QByteArray trun1 = be32(3);
    for (quint32 size : {5000u, 100u, 100u}) trun1 += be32(size);
    const QByteArray moof1 = box("moof", box("traf", fullBox("tfhd", 0, 0, be32(kTrack))
                                                      + fullBox("tfdt", 1, 0, be64(kClockStart))
                                                      + fullBox("trun", 0, 0x200, trun1)));
    const QByteArray moof2 = box("moof", box("traf", fullBox("tfhd", 0, 0x10, be32(kTrack) + be32(700))
                                                      + fullBox("trun", 0, 0x001, be32(2) + be32(0))));
    // Another track's fragment is skipped.
    const QByteArray otherMoof = box("moof", box("traf", fullBox("tfhd", 0, 0x10, be32(2) + be32(99999))
                                                          + fullBox("trun", 0, 0, be32(4))));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(QStringLiteral("fragmented.mp4")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(box("ftyp", QByteArray("isom") + be32(0)) + moov + moof1 + box("mdat", QByteArray(5200, 'x'))
               + otherMoof + moof2 + box("mdat", QByteArray(1400, 'x')));
    file.close();

    const Mp4VideoSamples samples = Mp4VideoSamples::read(file.fileName());
    QVERIFY(samples.isValid());
    QCOMPARE(samples.sampleCount(), 5);
    QCOMPARE(samples.totalBytes(), qint64(5000 + 100 + 100 + 700 + 700));
    // Frames at 0, 33, 67, 100 and 133 ms, counted from the first.
    QCOMPARE(samples.bytesBetween(0, 1), qint64(5000));
    QCOMPARE(samples.bytesBetween(30, 70), qint64(200));
    QCOMPARE(samples.bytesBetween(90, 200), qint64(1400));

    // A file cut short (a recording still being written) keeps what it has.
    QVERIFY(file.open(QIODevice::ReadWrite));
    file.resize(file.size() - (8 + 1400) - 10); // into the second fragment
    file.close();
    QCOMPARE(Mp4VideoSamples::read(file.fileName()).sampleCount(), 3);
    // Not an MP4.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("not a video");
    file.close();
    QVERIFY(!Mp4VideoSamples::read(file.fileName()).isValid());
}

QTEST_MAIN(TestMp4ExportOptions)
#include "tst_Mp4ExportOptions.moc"
