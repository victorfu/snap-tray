#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"
#include "video/IVideoTranscoder.h"

#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace SnapTray;

namespace {

constexpr int kFrameRate = 30;
constexpr int kSeconds = 3;
constexpr int kFrameCount = kFrameRate * kSeconds;
const QSize kFrameSize(320, 240);
// A 1 s GOP over 3 s yields keyframes at 0, 1, 2 s (3) plus possibly one at
// the end; the Media Foundation default GOP is several seconds, so 3 is the
// smallest count that proves the interval was applied.
constexpr int kMinimumKeyFrames = kSeconds;
constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;

// Moving content: a static image lets the encoder emit nothing but the GOP
// structure, which would still pass, but real motion is the honest case.
QImage frameAt(int index)
{
    QImage image(kFrameSize, QImage::Format_ARGB32);
    image.fill(Qt::darkGray);
    QPainter painter(&image);
    painter.fillRect(QRect((index * 7) % kFrameSize.width(), (index * 3) % kFrameSize.height(), 40, 40), Qt::yellow);
    return image;
}

QString encode(const QString& path, VideoRateControl mode, int keyFrameIntervalSeconds)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(IntermediateQuality::kConstantQualityValue);
    encoder->setRateControl(mode, IntermediateQuality::kConstantQualityValue);
    encoder->setKeyFrameIntervalSeconds(keyFrameIntervalSeconds);
    if (!encoder->start(path, kFrameSize, kFrameRate)) return encoder->lastError();
    for (int i = 0; i < kFrameCount; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer timer;
        timer.start();
        do {
            encoder->writeFrame(frameAt(i), qint64(i) * 1000 / kFrameRate);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (timer.elapsed() < kFrameAcceptTimeoutMs);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("frame %1 rejected").arg(i);
    }
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty() && !finished.wait(kFinishTimeoutMs)) return QStringLiteral("did not finish");
    if (!finished.first().at(0).toBool()) return encoder->lastError();
    qInfo() << "effective rate control:" << int(encoder->effectiveRateControl());
    return {};
}

// Counts compressed video samples flagged as clean points (IDR frames).
int countKeyFrames(const QString& path)
{
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, &reader))) return -1;
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    int keyFrames = 0;
    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &sample))) return -1;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        if (MFGetAttributeUINT32(sample.Get(), MFSampleExtension_CleanPoint, FALSE)) ++keyFrames;
    }
    return keyFrames;
}

} // namespace

class tst_MediaFoundationEncoderIntermediate : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(SUCCEEDED(MFStartup(MF_VERSION))); }
    void cleanupTestCase() { MFShutdown(); }
    void intermediateHasOneSecondKeyFrames();
    void defaultModeStillRecords();
};

void tst_MediaFoundationEncoderIntermediate::intermediateHasOneSecondKeyFrames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("intermediate.mp4"));
    const QString error = encode(path, VideoRateControl::ConstantQuality, IntermediateQuality::kKeyFrameIntervalSeconds);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    const VideoFileProbe probe = transcoder->probe(path);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, kFrameSize);
    QCOMPARE(probe.videoCodec, QString::fromLatin1(kVideoCodecH264));
    const int keyFrames = countKeyFrames(path);
    QVERIFY2(keyFrames >= kMinimumKeyFrames, qPrintable(QStringLiteral("key frames: %1").arg(keyFrames)));
}

void tst_MediaFoundationEncoderIntermediate::defaultModeStillRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("default.mp4"));
    const QString error = encode(path, VideoRateControl::Bitrate, 0);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    QVERIFY(transcoder->probe(path).valid);
    QVERIFY(countKeyFrames(path) >= 1);
}

QTEST_MAIN(tst_MediaFoundationEncoderIntermediate)
#include "tst_MediaFoundationEncoderIntermediate_win.moc"
