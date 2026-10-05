#include "encoding/FFmpegEncoder.h"
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

QTEST_GUILESS_MAIN(TestFFmpegEncoder)
#include "tst_FFmpegEncoder.moc"
