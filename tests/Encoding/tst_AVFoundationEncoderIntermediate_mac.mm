#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"
#include "video/IVideoTranscoder.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <memory>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#import <AVFoundation/AVFoundation.h>

using namespace SnapTray;

namespace {

constexpr int kFrameRate = 30;
constexpr int kSeconds = 3;
constexpr int kFrameCount = kFrameRate * kSeconds;
const QSize kFrameSize(320, 240);
constexpr int kMinimumKeyFrames = kSeconds;
constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;

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
    return {};
}

// Counts sync samples (frames without kCMSampleAttachmentKey_NotSync) in the
// compressed video track.
int countKeyFrames(const QString& path)
{
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path.toNSString()] options:nil];
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!track) return -1;
        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader) return -1;
        AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:nil];
        [reader addOutput:output];
        if (![reader startReading]) return -1;
        int keyFrames = 0;
        while (CMSampleBufferRef sample = [output copyNextSampleBuffer]) {
            CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
            bool notSync = false;
            if (attachments && CFArrayGetCount(attachments) > 0) {
                CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
                CFBooleanRef value = (CFBooleanRef)CFDictionaryGetValue(dict, kCMSampleAttachmentKey_NotSync);
                notSync = value && CFBooleanGetValue(value);
            }
            if (!notSync) ++keyFrames;
            CFRelease(sample);
        }
        return keyFrames;
    }
}

} // namespace

class tst_AVFoundationEncoderIntermediate : public QObject
{
    Q_OBJECT
private slots:
    void intermediateHasOneSecondKeyFrames();
    void defaultModeStillRecords();
};

void tst_AVFoundationEncoderIntermediate::intermediateHasOneSecondKeyFrames()
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

void tst_AVFoundationEncoderIntermediate::defaultModeStillRecords()
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

QTEST_MAIN(tst_AVFoundationEncoderIntermediate)
#include "tst_AVFoundationEncoderIntermediate_mac.moc"
