#include <QtTest/QtTest>

#include "encoding/WebPAnimEncoder.h"

#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <webp/demux.h>

#include <cstdlib>
#include <vector>

namespace {

constexpr QSize kFrameSize(64, 48);
// Lossy WebP rounds through YUV; flat colors stay within a few levels.
constexpr int kChannelTolerance = 10;

QImage solidFrame(const QColor& color)
{
    QImage image(kFrameSize, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

// Left half fully transparent, right half opaque green.
QImage halfTransparentFrame()
{
    QImage image(kFrameSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = image.width() / 2; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(0, 200, 0));
        }
    }
    return image;
}

bool closeTo(const QColor& actual, const QColor& expected)
{
    return std::abs(actual.red() - expected.red()) <= kChannelTolerance
        && std::abs(actual.green() - expected.green()) <= kChannelTolerance
        && std::abs(actual.blue() - expected.blue()) <= kChannelTolerance
        && std::abs(actual.alpha() - expected.alpha()) <= kChannelTolerance;
}

// Decodes every frame of an animated WebP into RGBA images.
std::vector<QImage> decodeFrames(const QByteArray& bytes)
{
    std::vector<QImage> frames;
    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) return frames;
    options.color_mode = MODE_RGBA;
    options.use_threads = 0;

    WebPData data;
    data.bytes = reinterpret_cast<const uint8_t*>(bytes.constData());
    data.size = static_cast<size_t>(bytes.size());
    WebPAnimDecoder* decoder = WebPAnimDecoderNew(&data, &options);
    if (!decoder) return frames;

    WebPAnimInfo info;
    if (WebPAnimDecoderGetInfo(decoder, &info)) {
        while (WebPAnimDecoderHasMoreFrames(decoder)) {
            uint8_t* buffer = nullptr;
            int timestamp = 0;
            if (!WebPAnimDecoderGetNext(decoder, &buffer, &timestamp)) break;
            const QImage view(buffer, int(info.canvas_width), int(info.canvas_height),
                              int(info.canvas_width) * 4, QImage::Format_RGBA8888);
            frames.push_back(view.copy());
        }
    }
    WebPAnimDecoderDelete(decoder);
    return frames;
}

} // namespace

class tst_WebPAnimEncoder : public QObject
{
    Q_OBJECT

private slots:
    void framesRoundTripThroughImport();
};

void tst_WebPAnimEncoder::framesRoundTripThroughImport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.webp"));

    const QColor red(220, 30, 30);
    const QColor blue(30, 40, 210);
    const std::vector<QImage> input{solidFrame(red), halfTransparentFrame(), solidFrame(blue)};

    {
        WebPAnimationEncoder encoder;
        encoder.setQuality(90);
        QVERIFY(encoder.start(path, kFrameSize, 10));
        for (size_t i = 0; i < input.size(); ++i) {
            encoder.writeFrame(input[i], qint64(i) * 100);
        }
        QCOMPARE(encoder.framesWritten(), qint64(input.size()));
        QSignalSpy finished(&encoder, &WebPAnimationEncoder::finished);
        encoder.finish();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(0).toBool());
    }

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const std::vector<QImage> frames = decodeFrames(file.readAll());
    QCOMPARE(frames.size(), input.size());

    for (const QImage& frame : frames) {
        QCOMPARE(frame.size(), kFrameSize);
    }

    const QPoint center(kFrameSize.width() / 2, kFrameSize.height() / 2);
    QVERIFY(closeTo(frames[0].pixelColor(center), red));
    QVERIFY(closeTo(frames[2].pixelColor(center), blue));

    const QPoint left(kFrameSize.width() / 4, kFrameSize.height() / 2);
    const QPoint right(kFrameSize.width() * 3 / 4, kFrameSize.height() / 2);
    QCOMPARE(frames[1].pixelColor(left).alpha(), 0);
    QVERIFY(closeTo(frames[1].pixelColor(right), QColor(0, 200, 0)));
}

QTEST_MAIN(tst_WebPAnimEncoder)
#include "tst_WebPAnimEncoder.moc"
