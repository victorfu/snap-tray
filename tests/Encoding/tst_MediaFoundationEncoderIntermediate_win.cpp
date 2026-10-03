#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"
#include "video/IVideoTranscoder.h"

#include <QtTest>
#include <QFileInfo>
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
constexpr int kLowQuality = 30;
constexpr int kHighQuality = 95;
// Quality mode must visibly change the output; high quality has to be at
// least this many times larger than low quality for identical content.
constexpr double kQualitySizeRatio = 1.3;
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

QString encode(const QString& path, VideoRateControl mode, int keyFrameIntervalSeconds,
               VideoRateControl* effective = nullptr, int quality = IntermediateQuality::kConstantQualityValue)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(quality);
    encoder->setRateControl(mode, quality);
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
    if (effective) *effective = encoder->effectiveRateControl();
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
    void constantQualityChangesOutputSize();
};

void tst_MediaFoundationEncoderIntermediate::intermediateHasOneSecondKeyFrames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("intermediate.mp4"));
    VideoRateControl effective = VideoRateControl::Bitrate;
    const QString error = encode(path, VideoRateControl::ConstantQuality, IntermediateQuality::kKeyFrameIntervalSeconds, &effective);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(effective == VideoRateControl::ConstantQuality || effective == VideoRateControl::Bitrate);
    qInfo() << "intermediate effective rate control:" << int(effective);
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    const VideoFileProbe probe = transcoder->probe(path);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, kFrameSize);
    QCOMPARE(probe.videoCodec, QString::fromLatin1(kVideoCodecH264));
    const int keyFrames = countKeyFrames(path);
    QVERIFY2(keyFrames >= kMinimumKeyFrames, qPrintable(QStringLiteral("key frames: %1").arg(keyFrames)));
    qInfo() << "key frames:" << keyFrames;
}

void tst_MediaFoundationEncoderIntermediate::defaultModeStillRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("default.mp4"));
    VideoRateControl effective = VideoRateControl::ConstantQuality;
    const QString error = encode(path, VideoRateControl::Bitrate, 0, &effective);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(effective == VideoRateControl::Bitrate);
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    QVERIFY(transcoder->probe(path).valid);
    QVERIFY(countKeyFrames(path) >= 1);
}

void tst_MediaFoundationEncoderIntermediate::constantQualityChangesOutputSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString lowPath = dir.filePath(QStringLiteral("low.mp4"));
    const QString highPath = dir.filePath(QStringLiteral("high.mp4"));
    QString error = encode(lowPath, VideoRateControl::ConstantQuality, 1, nullptr, kLowQuality);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = encode(highPath, VideoRateControl::ConstantQuality, 1, nullptr, kHighQuality);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const qint64 lowSize = QFileInfo(lowPath).size();
    const qint64 highSize = QFileInfo(highPath).size();
    qInfo() << "size low:" << lowSize << "high:" << highSize
            << "ratio:" << (lowSize > 0 ? double(highSize) / double(lowSize) : 0.0);
    QVERIFY2(highSize > lowSize * kQualitySizeRatio,
             qPrintable(QStringLiteral("low %1 high %2").arg(lowSize).arg(highSize)));
}

QTEST_MAIN(tst_MediaFoundationEncoderIntermediate)
#include "tst_MediaFoundationEncoderIntermediate_win.moc"
