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

QImage frameAt(int index, const QSize& size = kSize)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.fillRect(QRect(0, index * kBarStep, size.width(), kBarHeight), Qt::black);
    return image;
}

int barRowOf(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        if (qGray(image.pixel(image.width() / 2, y)) < 128) return y;
    }
    return -1;
}

QString createRecording(const QString& path, const QSize& size = kSize)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(IntermediateQuality::kConstantQualityValue);
    encoder->setRateControl(VideoRateControl::ConstantQuality, IntermediateQuality::kConstantQualityValue);
    encoder->setKeyFrameIntervalSeconds(IntermediateQuality::kKeyFrameIntervalSeconds);
    if (!encoder->start(path, size, kFrameRate)) return encoder->lastError();
    for (int i = 0; i < kFrameCount; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer timer;
        timer.start();
        do {
            encoder->writeFrame(frameAt(i, size), qint64(i) * kFrameIntervalMs);
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
    void readerHonoursApertureOnPaddedHeight();
    void readerRejectsDescendingRequests();
    void sourceHonoursTrimAndCrop();
    void sourceFullRangeCountsFrames();
    void openRejectsTinyCrop();
    void openFailsOnMissingFile();

private:
    QTemporaryDir m_dir;
    QString m_path;
    QString m_paddedPath;
    QSize m_paddedSize;
};

void tst_FrameSource::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // Linux has neither a native encoder nor an offline reader.
    if (!std::unique_ptr<IVideoEncoder>(IVideoEncoder::createNativeEncoder())) QSKIP("No native encoder on this platform");
    if (!IVideoFrameReader::createOffline()) QSKIP("No offline frame reader on this platform");
    m_path = m_dir.filePath(QStringLiteral("scroll.mp4"));
    const QString error = createRecording(m_path);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // Height 232 is not a multiple of 16, so the decoder pads to 240 coded rows
    // and the display aperture must be honoured. Fall back to 248 if refused.
    for (const QSize candidate : {QSize(320, 232), QSize(320, 248)}) {
        m_paddedPath = m_dir.filePath(QStringLiteral("padded%1.mp4").arg(candidate.height()));
        if (createRecording(m_paddedPath, candidate).isEmpty()) {
            m_paddedSize = candidate;
            break;
        }
    }
    QVERIFY2(m_paddedSize.isValid(), "encoder refused both 232 and 248 row frames");
    qInfo() << "padded fixture height:" << m_paddedSize.height();
}

void tst_FrameSource::readerHonoursApertureOnPaddedHeight()
{
    auto reader = IVideoFrameReader::createOffline();
    if (!reader) QSKIP("No frame reader on this platform");
    QVERIFY2(reader->load(m_paddedPath), qPrintable(reader->lastError()));
    QCOMPARE(reader->videoSize(), m_paddedSize);
    for (int i = 0; i < kFrameCount; ++i) {
        const QImage frame = reader->frameAt(qint64(i) * kFrameIntervalMs);
        QVERIFY2(!frame.isNull(), qPrintable(reader->lastError()));
        QCOMPARE(frame.size(), m_paddedSize);
        QVERIFY2(qAbs(barRowOf(frame) - i * kBarStep) <= 1,
                 qPrintable(QStringLiteral("frame %1 bar at %2").arg(i).arg(barRowOf(frame))));
    }
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source);
    QVERIFY2(source->open(m_paddedPath, 0, -1, QRect()), qPrintable(source->lastError()));
    QCOMPARE(source->frameSize(), m_paddedSize);
}

void tst_FrameSource::readerDecodesAscendingFrames()
{
    auto reader = IVideoFrameReader::createOffline();
    if (!reader) QSKIP("No frame reader on this platform");
    QVERIFY2(reader->load(m_path), qPrintable(reader->lastError()));
    QCOMPARE(reader->videoSize(), kSize);
    QVERIFY(qAbs(reader->frameRate() - kFrameRate) < 0.5);
    QVERIFY(qAbs(reader->duration() - kFrameCount * kFrameIntervalMs) <= kFrameIntervalMs);
    // Decoded H.264 is lossy: the bar row must match within one row.
    for (int i = 0; i < kFrameCount; ++i) {
        const QImage frame = reader->frameAt(qint64(i) * kFrameIntervalMs);
        QVERIFY2(!frame.isNull(), qPrintable(reader->lastError()));
        // Native readers keep their platform format; the Longshot adapter
        // below owns normalization to RGB32 (macOS returns opaque ARGB32).
        QVERIFY(frame.format() == QImage::Format_RGB32 || frame.format() == QImage::Format_ARGB32);
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
    auto reader = IVideoFrameReader::createOffline();
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
        QCOMPARE(frame->format(), QImage::Format_RGB32);
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
    QCOMPARE(count, source->expectedFrameCount());
    QVERIFY(count >= kFrameCount);
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
