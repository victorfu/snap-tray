#include "encoding/FFmpegEncoder.h"
#include "video/IVideoTranscoder.h"
#include <QFileInfo>
#include <memory>
#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class TestFFmpegEncoder : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip();
    void invalidConfiguration();
    void abortAndFailurePreserveDestination();
    void emptyRecordingAndReuse();
    void fragmentedCrashLeavesPlayablePrefix_data();
    void fragmentedCrashLeavesPlayablePrefix();
    void unfragmentedCrashLeavesNothingAtThePath();
    void fragmentedNeverOverwritesExistingFile();
    void writtenBytesAreReportedBeforeCommit();
};

void TestFFmpegEncoder::roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FFmpegEncoder encoder;
    QVERIFY2(encoder.isAvailable(), "Install a system FFmpeg build with libx264 or libopenh264");
    const QString path = dir.filePath(QStringLiteral("錄影.mp4"));
    QSignalSpy finished(&encoder, &IVideoEncoder::finished);
    QVERIFY2(encoder.start(path, QSize(128, 96), 30), qPrintable(encoder.lastError()));
    QImage image(128, 96, QImage::Format_ARGB32);
    image.fill(Qt::green);
    // Include a gap (dropped frames), then a duplicate and regressing timestamp.
    const QList<qint64> timestamps{1000, 1033, 1067, 1200, 1200, 1190, 1300};
    for (qint64 timestamp : timestamps) {
        encoder.writeFrame(image, timestamp);
        QVERIFY2(encoder.isRunning(), qPrintable(encoder.lastError()));
    }
    QCOMPARE(encoder.framesWritten(), timestamps.size());
    encoder.finish();
    QCOMPARE(finished.count(), 1);
    QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(encoder.lastError()));
    QVERIFY(!encoder.isRunning());

    AVFormatContext* format = nullptr;
    const QByteArray filename = QFile::encodeName(path);
    QCOMPARE(avformat_open_input(&format, filename.constData(), nullptr, nullptr), 0);
    auto closeFormat = qScopeGuard([&] { avformat_close_input(&format); });
    QVERIFY(avformat_find_stream_info(format, nullptr) >= 0);
    QCOMPARE(format->nb_streams, 1u); // silent video only
    auto* stream = format->streams[0];
    QCOMPARE(stream->codecpar->codec_id, AV_CODEC_ID_H264);
    QCOMPARE(stream->codecpar->width, 128);
    QCOMPARE(stream->codecpar->height, 96);
    QVERIFY(format->duration >= 300000 && format->duration <= 400000);
    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    QVERIFY(decoder);
    AVCodecContext* codec = avcodec_alloc_context3(decoder);
    QVERIFY(codec);
    auto freeCodec = qScopeGuard([&] { avcodec_free_context(&codec); });
    QCOMPARE(avcodec_parameters_to_context(codec, stream->codecpar), 0);
    QCOMPARE(avcodec_open2(codec, decoder, nullptr), 0);
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    QVERIFY(packet && frame);
    auto freeBuffers = qScopeGuard([&] { av_packet_free(&packet); av_frame_free(&frame); });
    int decoded = 0;
    qint64 previousPts = -1;
    auto drain = [&] {
        while (avcodec_receive_frame(codec, frame) == 0) {
            ++decoded;
            QVERIFY(frame->pts > previousPts);
            previousPts = frame->pts;
            // Limited-range BT.601 green is approximately Y=145 U=54 V=34.
            QVERIFY(qAbs(frame->data[0][0] - 145) < 12);
            QVERIFY(qAbs(frame->data[1][0] - 54) < 12);
            QVERIFY(qAbs(frame->data[2][0] - 34) < 12);
        }
    };
    while (av_read_frame(format, packet) >= 0) {
        QCOMPARE(avcodec_send_packet(codec, packet), 0);
        av_packet_unref(packet);
        drain();
    }
    QCOMPARE(avcodec_send_packet(codec, nullptr), 0);
    drain();
    QCOMPARE(decoded, timestamps.size());
}

void TestFFmpegEncoder::invalidConfiguration()
{
    QTemporaryDir dir;
    FFmpegEncoder encoder;
    const QString path = dir.filePath("invalid.mp4");
    QVERIFY(!encoder.start(path, QSize(127, 96), 30));
    QVERIFY(!encoder.start(path, QSize(128, 95), 30));
    QVERIFY(!encoder.start(path, QSize(128, 96), 0));
    QVERIFY(!encoder.start(path, QSize(128, 96), 241));
    QVERIFY(!encoder.start(dir.path(), QSize(128, 96), 30));
    QVERIFY(!encoder.lastError().isEmpty());
    QVERIFY(!QFile::exists(path));
}

void TestFFmpegEncoder::abortAndFailurePreserveDestination()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("existing.mp4");
    QFile existing(path);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("original"), 8);
    existing.close();
    QImage image(128, 96, QImage::Format_RGB32);
    image.fill(Qt::red);
    FFmpegEncoder encoder;
    QVERIFY(encoder.start(path, image.size(), 30));
    encoder.writeFrame(image);
    encoder.abort();
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("original"));
    existing.close();
    QVERIFY(encoder.start(path, image.size(), 30));
    encoder.writeFrame(QImage(64, 64, QImage::Format_RGB32));
    QVERIFY(!encoder.isRunning());
    QVERIFY(!encoder.lastError().isEmpty());
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("original"));
}

void TestFFmpegEncoder::emptyRecordingAndReuse()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("out.mp4");
    FFmpegEncoder encoder;
    QSignalSpy finished(&encoder, &IVideoEncoder::finished);
    QVERIFY(encoder.start(path, QSize(128, 96), 30));
    encoder.finish();
    QVERIFY(!finished.takeFirst().at(0).toBool());
    QVERIFY(!QFile::exists(path));
    QVERIFY(encoder.start(path, QSize(128, 96), 30));
    QVERIFY(encoder.lastError().isEmpty());
    QImage frame(128, 96, QImage::Format_RGBA8888);
    frame.fill(Qt::blue);
    for (int i = 0; i < 300; ++i) {
        encoder.writeFrame(frame);
    }
    encoder.finish();
    QVERIFY(finished.takeFirst().at(0).toBool());
    QVERIFY(QFileInfo(path).size() > 0);
    AVFormatContext* format = nullptr;
    QCOMPARE(avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr), 0);
    auto closeFormat = qScopeGuard([&] { avformat_close_input(&format); });
    QVERIFY(avformat_find_stream_info(format, nullptr) >= 0);
    // The implicit 30 fps clock must not accumulate rounded 33 ms intervals.
    QVERIFY(qAbs(format->duration - 10000000) <= 1000);
}

namespace {
void writeSeconds(FFmpegEncoder& encoder, int fromMs, int toMs, const QSize& size = QSize(128, 96))
{
    QImage image(size, QImage::Format_ARGB32);
    for (int t = fromMs; t < toMs; t += 33) {
        image.fill(QColor::fromHsv((t / 10) % 360, 200, 200));
        encoder.writeFrame(image, t);
    }
}

// The strict check players and the save flow rely on: stream parameters
// (including the decoder configuration) can be read from the file.
bool hasStreamInfo(const QString& path)
{
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr) != 0) return false;
    const bool ok = avformat_find_stream_info(format, nullptr) >= 0 && format->nb_streams >= 1
        && format->streams[0]->codecpar->width > 0;
    avformat_close_input(&format);
    return ok;
}
}

void TestFFmpegEncoder::fragmentedCrashLeavesPlayablePrefix_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("small (software)") << QSize(128, 96);
    // Large enough for the hardware (VAAPI) path where available: its SPS/PPS
    // arrive with the first packet, after an empty moov would be written.
    QTest::newRow("1080p") << QSize(1920, 1080);
}

void TestFFmpegEncoder::fragmentedCrashLeavesPlayablePrefix()
{
    QFETCH(QSize, size);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FFmpegEncoder encoder;
    QVERIFY(encoder.isAvailable());
    QVERIFY(encoder.setFragmentedOutput(true));
    QVERIFY(encoder.isFragmentedOutput());
    const QString path = dir.filePath(QStringLiteral("SnapTray_Recording_crash.mp4"));
    QVERIFY2(encoder.start(path, size, 30), qPrintable(encoder.lastError()));
    // The file is written where it belongs from the start.
    QVERIFY(QFileInfo::exists(path));
    writeSeconds(encoder, 0, 4500, size);  // keyframes (fragments) at 0, 2 and 4 s

    // A crash: the process is gone, the file stays exactly as it is now.
    const QString crashed = dir.filePath(QStringLiteral("crashed.mp4"));
    QVERIFY(QFile::copy(path, crashed));
    std::unique_ptr<IVideoTranscoder> transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    const VideoFileProbe probe = transcoder->probe(crashed);
    QVERIFY2(probe.valid, "an interrupted fragmented recording must still open");
    QVERIFY2(probe.durationMs >= 1900, qPrintable(QString::number(probe.durationMs)));
    QCOMPARE(probe.videoSize, size);
    QVERIFY(hasStreamInfo(crashed));

    // Aborting (the user cancelled) removes it; finishing would have kept it.
    encoder.abort();
    QVERIFY(!QFileInfo::exists(path));
}

void TestFFmpegEncoder::unfragmentedCrashLeavesNothingAtThePath()
{
    QTemporaryDir dir;
    FFmpegEncoder encoder;
    const QString path = dir.filePath(QStringLiteral("SnapTray_Recording_plain.mp4"));
    QVERIFY(encoder.start(path, QSize(128, 96), 30));
    writeSeconds(encoder, 0, 1000);
    // Normal mode writes to a hidden temporary file until finish(): this is
    // why recordings use fragmented output.
    QVERIFY(!QFileInfo::exists(path));
    QSignalSpy finished(&encoder, &IVideoEncoder::finished);
    encoder.finish();
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.first().first().toBool());
    QVERIFY(QFileInfo::exists(path));
}

void TestFFmpegEncoder::fragmentedNeverOverwritesExistingFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("existing.mp4"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("keep me");
    }
    FFmpegEncoder encoder;
    QVERIFY(encoder.setFragmentedOutput(true));
    QVERIFY(encoder.start(path, QSize(128, 96), 30));
    writeSeconds(encoder, 0, 500);
    encoder.abort();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("keep me"));

    // A finished fragmented recording is a normal, complete file.
    FFmpegEncoder second;
    QVERIFY(second.setFragmentedOutput(true));
    const QString fresh = dir.filePath(QStringLiteral("fresh.mp4"));
    QVERIFY(second.start(fresh, QSize(128, 96), 30));
    writeSeconds(second, 0, 3000);
    QSignalSpy finished(&second, &IVideoEncoder::finished);
    second.finish();
    QVERIFY(finished.first().first().toBool());
    const VideoFileProbe probe = IVideoTranscoder::create()->probe(fresh);
    QVERIFY(probe.valid);
    QVERIFY(hasStreamInfo(fresh));
    QVERIFY2(qAbs(probe.durationMs - 3000) <= 100, qPrintable(QString::number(probe.durationMs)));
}

void TestFFmpegEncoder::writtenBytesAreReportedBeforeCommit()
{
    // PR #171 review: output goes to a temporary file until finish() commits
    // it, so the destination has no size while recording; the encoder
    // reports what it has written for the disk guard.
    QTemporaryDir dir;
    FFmpegEncoder encoder;
    QVERIFY2(encoder.isAvailable(), "Install a system FFmpeg build with libx264 or libopenh264");
    const QString path = dir.filePath(QStringLiteral("growing.mp4"));
    QVERIFY2(encoder.start(path, QSize(320, 240), 30), qPrintable(encoder.lastError()));
    const auto written = encoder.writtenBytesCounter();
    QVERIFY(written);
    QImage image(320, 240, QImage::Format_ARGB32);
    quint32 seed = 1;
    for (int i = 0; i < 90; ++i) {
        // Noise does not compress: the muxer flushes to disk during recording.
        for (int y = 0; y < image.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                seed = seed * 1664525u + 1013904223u;
                line[x] = 0xff000000u | (seed >> 8);
            }
        }
        encoder.writeFrame(image, i * 33);
    }
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY2(written->load() > 0, "nothing reported while recording");
    encoder.finish();
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(written->load(), QFileInfo(path).size());
}

QTEST_GUILESS_MAIN(TestFFmpegEncoder)
#include "tst_FFmpegEncoder.moc"
