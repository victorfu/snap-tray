#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "video/IVideoFrameReader.h"

#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

using namespace SnapTray;
using namespace SnapTray::Longshot;

namespace {

constexpr int kFrameRate = 10;
constexpr int kFrameCount = 20; // 2 s
constexpr int kFrameIntervalMs = 1000 / kFrameRate;
const QSize kSize(320, 240);
constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;
// Each frame paints its index as a horizontal bar position so decoded frames
// can be identified by sampling one pixel row.
constexpr int kBarHeight = 20;
constexpr int kBarStep = 10;

QImage frameAt(int index)
{
    QImage image(kSize, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.fillRect(QRect(0, index * kBarStep, kSize.width(), kBarHeight), Qt::black);
    return image;
}

int barRowOf(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        if (qGray(image.pixel(image.width() / 2, y)) < 128) return y;
    }
    return -1;
}

QString createRecording(const QString& path)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(IntermediateQuality::kConstantQualityValue);
    encoder->setRateControl(VideoRateControl::ConstantQuality, IntermediateQuality::kConstantQualityValue);
    encoder->setKeyFrameIntervalSeconds(IntermediateQuality::kKeyFrameIntervalSeconds);
    if (!encoder->start(path, kSize, kFrameRate)) return encoder->lastError();
    for (int i = 0; i < kFrameCount; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer timer;
        timer.start();
        do {
            encoder->writeFrame(frameAt(i), qint64(i) * kFrameIntervalMs);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (timer.elapsed() < kFrameAcceptTimeoutMs);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("frame %1 rejected").arg(i);
    }
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty() && !finished.wait(kFinishTimeoutMs)) return QStringLiteral("did not finish");
    return finished.first().at(0).toBool() ? QString() : encoder->lastError();
}

} // namespace

class tst_FrameSource : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void readerDecodesAscendingFrames();
    void readerRejectsDescendingRequests();
    void sourceHonoursTrimAndCrop();
    void sourceFullRangeCountsFrames();
    void openRejectsTinyCrop();
    void openFailsOnMissingFile();

private:
    QTemporaryDir m_dir;
    QString m_path;
};

void tst_FrameSource::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_path = m_dir.filePath(QStringLiteral("scroll.mp4"));
    const QString error = createRecording(m_path);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void tst_FrameSource::readerDecodesAscendingFrames()
{
    auto reader = IVideoFrameReader::create();
    if (!reader) QSKIP("No frame reader on this platform");
    QVERIFY2(reader->load(m_path), qPrintable(reader->lastError()));
    QCOMPARE(reader->videoSize(), kSize);
    QVERIFY(qAbs(reader->frameRate() - kFrameRate) < 0.5);
    QVERIFY(qAbs(reader->duration() - kFrameCount * kFrameIntervalMs) <= kFrameIntervalMs);
    // Decoded H.264 is lossy: the bar row must match within one row.
    for (int i = 0; i < kFrameCount; ++i) {
        const QImage frame = reader->frameAt(qint64(i) * kFrameIntervalMs);
        QVERIFY2(!frame.isNull(), qPrintable(reader->lastError()));
        QCOMPARE(frame.format(), QImage::Format_RGB32);
        QVERIFY2(qAbs(barRowOf(frame) - i * kBarStep) <= 1,
                 qPrintable(QStringLiteral("frame %1 bar at %2").arg(i).arg(barRowOf(frame))));
    }
    // Past the end holds the last frame.
    const QImage tail = reader->frameAt(qint64(kFrameCount + 5) * kFrameIntervalMs);
    QVERIFY(!tail.isNull());
    QVERIFY(qAbs(barRowOf(tail) - (kFrameCount - 1) * kBarStep) <= 1);
}

void tst_FrameSource::readerRejectsDescendingRequests()
{
    auto reader = IVideoFrameReader::create();
    if (!reader) QSKIP("No frame reader on this platform");
    QVERIFY(reader->load(m_path));
    QVERIFY(!reader->frameAt(500).isNull());
    QVERIFY(reader->frameAt(100).isNull());
    QVERIFY(!reader->lastError().isEmpty());
}

void tst_FrameSource::sourceHonoursTrimAndCrop()
{
    auto source = FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    const QRect crop(40, 20, 200, 160); // even-aligned, inside the frame
    QVERIFY2(source->open(m_path, 500, 1200, crop), qPrintable(source->lastError()));
    QCOMPARE(source->frameSize(), crop.size());
    QCOMPARE(source->expectedFrameCount(), 7); // 500,600,...,1100
    int count = 0;
    qint64 lastT = -1;
    while (auto frame = source->next(&lastT)) {
        QCOMPARE(frame->size(), crop.size());
        QVERIFY(lastT >= 500 && lastT < 1200);
        // Frame at t has its bar at row t/100*10 in full-frame coordinates; in
        // crop-local coordinates that is minus crop.y().
        const int expected = int(lastT / kFrameIntervalMs) * kBarStep - crop.y();
        const int actual = barRowOf(*frame);
        if (expected >= 0 && expected + kBarHeight <= crop.height()) {
            QVERIFY2(qAbs(actual - expected) <= 1,
                     qPrintable(QStringLiteral("t=%1 bar %2 expected %3").arg(lastT).arg(actual).arg(expected)));
        }
        ++count;
    }
    QCOMPARE(count, 7);
}

void tst_FrameSource::sourceFullRangeCountsFrames()
{
    auto source = FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    QVERIFY(source->open(m_path, 0, -1, QRect()));
    QCOMPARE(source->frameSize(), kSize);
    int count = 0;
    qint64 t = 0;
    while (source->next(&t)) ++count;
    QCOMPARE(count, kFrameCount);
}

void tst_FrameSource::openRejectsTinyCrop()
{
    auto source = FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    QVERIFY(!source->open(m_path, 0, -1, QRect(0, 0, 32, 32)));
    QVERIFY(source->lastError().contains(QStringLiteral("crop")));
}

void tst_FrameSource::openFailsOnMissingFile()
{
    auto source = FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    QVERIFY(!source->open(m_dir.filePath(QStringLiteral("missing.mp4")), 0, -1, QRect()));
    QVERIFY(!source->lastError().isEmpty());
}

QTEST_MAIN(tst_FrameSource)
#include "tst_FrameSource.moc"
