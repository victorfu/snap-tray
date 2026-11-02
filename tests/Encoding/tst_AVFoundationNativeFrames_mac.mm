// Native capture surfaces (CVPixelBuffer) encoded by AVFoundationEncoder
// without a CPU copy (#154), checked against the QImage path.

#include "IVideoEncoder.h"

#include <QtTest>
#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <functional>
#include <memory>

#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>

namespace {

constexpr int kFrameRate = 30;
constexpr int kFrameCount = kFrameRate;  // one second
const QSize kFrameSize(640, 360);
// Like ScreenCaptureKit's stream queue: the encoder must hand surfaces back.
constexpr int kPoolBufferLimit = 6;
constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;
// H.264 at the default bitrate, measured in the middle of wide flat bars.
// A color conversion (display profile to BT.709) moves mid-tones by 10+.
constexpr int kColorTolerance = 6;

const QVector<QColor>& barColors()
{
    // Mid-tones: pure primaries would hide a gamut or transfer conversion.
    static const QVector<QColor> colors = {
        QColor(200, 80, 60), QColor(60, 160, 90), QColor(70, 90, 200), QColor(128, 128, 128),
        QColor(220, 180, 40), QColor(40, 180, 200), QColor(180, 60, 160), QColor(30, 30, 30)};
    return colors;
}

QImage colorBars()
{
    QImage image(kFrameSize, QImage::Format_ARGB32);
    const int barWidth = kFrameSize.width() / barColors().size();
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            line[x] = barColors()[qMin(x / barWidth, int(barColors().size()) - 1)].rgba();
        }
    }
    return image;
}

std::shared_ptr<void> adoptPixelBuffer(CVPixelBufferRef buffer)
{
    return std::shared_ptr<void>(buffer, [](void* b) { CVPixelBufferRelease(static_cast<CVPixelBufferRef>(b)); });
}

// IOSurface-backed BGRA pool, the kind of buffer ScreenCaptureKit delivers.
CVPixelBufferPoolRef createPool(const QSize& size, OSType format)
{
    NSDictionary* attributes = @{
        (NSString*)kCVPixelBufferPixelFormatTypeKey: @(format),
        (NSString*)kCVPixelBufferWidthKey: @(size.width()),
        (NSString*)kCVPixelBufferHeightKey: @(size.height()),
        (NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };
    CVPixelBufferPoolRef pool = nullptr;
    CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr, (__bridge CFDictionaryRef)attributes, &pool);
    return pool;
}

// A pool buffer filled with `image`, or null when the pool is at its limit.
// Tagged with a display color space, as ScreenCaptureKit tags its buffers.
std::shared_ptr<void> surfaceFrom(CVPixelBufferPoolRef pool, const QImage& image)
{
    NSDictionary* aux = @{(NSString*)kCVPixelBufferPoolAllocationThresholdKey: @(kPoolBufferLimit)};
    CVPixelBufferRef buffer = nullptr;
    if (CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(
            kCFAllocatorDefault, pool, (__bridge CFDictionaryRef)aux, &buffer) != kCVReturnSuccess) {
        return nullptr;
    }
    CVPixelBufferLockBaseAddress(buffer, 0);
    auto* base = static_cast<uchar*>(CVPixelBufferGetBaseAddress(buffer));
    const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
    for (int y = 0; y < image.height(); ++y) {
        memcpy(base + y * stride, image.constScanLine(y), size_t(image.width()) * 4);
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    CGColorSpaceRef displaySpace = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
    CVBufferSetAttachment(buffer, kCVImageBufferCGColorSpaceKey, displaySpace, kCVAttachmentMode_ShouldPropagate);
    CGColorSpaceRelease(displaySpace);
    return adoptPixelBuffer(buffer);
}

// Offers one frame until the encoder takes it (it drops while busy).
QString writeUntilAccepted(IVideoEncoder& encoder, const std::function<void()>& write)
{
    const qint64 before = encoder.framesWritten();
    QElapsedTimer timer;
    timer.start();
    while (encoder.framesWritten() == before && timer.elapsed() < kFrameAcceptTimeoutMs) {
        write();
        if (encoder.framesWritten() == before) QTest::qWait(5);
    }
    return encoder.framesWritten() == before + 1 ? QString() : QStringLiteral("frame not accepted");
}

QString finish(IVideoEncoder& encoder)
{
    QSignalSpy finished(&encoder, &IVideoEncoder::finished);
    encoder.finish();
    if (finished.isEmpty() && !finished.wait(kFinishTimeoutMs)) return QStringLiteral("did not finish");
    return finished.first().at(0).toBool() ? QString() : encoder.lastError();
}

std::unique_ptr<IVideoEncoder> startEncoder(const QString& path)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (encoder && !encoder->start(path, kFrameSize, kFrameRate)) encoder.reset();
    return encoder;
}

// The middle frame of the video, decoded to BGRA.
QImage decodeMiddleFrame(const QString& path)
{
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path.toNSString()] options:nil];
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!track) return {};
        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader) return {};
        NSDictionary* settings = @{(NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
        AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
        [reader addOutput:output];
        if (![reader startReading]) return {};
        QImage result;
        for (int i = 0; i <= kFrameCount / 2; ++i) {
            CMSampleBufferRef sample = [output copyNextSampleBuffer];
            if (!sample) break;
            if (i == kFrameCount / 2 || result.isNull()) {
                CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
                CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
                result = QImage(static_cast<const uchar*>(CVPixelBufferGetBaseAddress(pixels)),
                                int(CVPixelBufferGetWidth(pixels)), int(CVPixelBufferGetHeight(pixels)),
                                int(CVPixelBufferGetBytesPerRow(pixels)), QImage::Format_ARGB32)
                             .copy();
                CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
            }
            CFRelease(sample);
        }
        [reader cancelReading];
        return result;
    }
}

QString compareBars(const QImage& decoded)
{
    if (decoded.size() != kFrameSize) return QStringLiteral("decoded size %1x%2").arg(decoded.width()).arg(decoded.height());
    const int barWidth = kFrameSize.width() / barColors().size();
    for (int i = 0; i < barColors().size(); ++i) {
        const QColor actual = decoded.pixelColor(i * barWidth + barWidth / 2, kFrameSize.height() / 2);
        const QColor expected = barColors()[i];
        if (qAbs(actual.red() - expected.red()) > kColorTolerance
            || qAbs(actual.green() - expected.green()) > kColorTolerance
            || qAbs(actual.blue() - expected.blue()) > kColorTolerance) {
            return QStringLiteral("bar %1: expected %2, got %3").arg(i).arg(expected.name(), actual.name());
        }
    }
    return {};
}

} // namespace

class tst_AVFoundationNativeFrames : public QObject
{
    Q_OBJECT
private slots:
    void nativeSurfacesEncodeColorBars();
    void imagePathStillEncodesColorBars();
    void mismatchedSurfaceIsDeclined_data();
    void mismatchedSurfaceIsDeclined();
};

void tst_AVFoundationNativeFrames::nativeSurfacesEncodeColorBars()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("native.mp4"));
    auto encoder = startEncoder(path);
    QVERIFY(encoder);
    CVPixelBufferPoolRef pool = createPool(kFrameSize, kCVPixelFormatType_32BGRA);
    QVERIFY(pool);
    const auto releasePool = qScopeGuard([pool] { CVPixelBufferPoolRelease(pool); });
    const QImage bars = colorBars();

    for (int i = 0; i < kFrameCount; ++i) {
        // A small pool runs dry if the encoder keeps surfaces it has encoded.
        std::shared_ptr<void> surface;
        QTRY_VERIFY_WITH_TIMEOUT((surface = surfaceFrom(pool, bars)) != nullptr, kFrameAcceptTimeoutMs);
        bool taken = true;
        const QString error = writeUntilAccepted(*encoder, [&] {
            taken = encoder->writeNativeFrame(surface, qint64(i) * 1000 / kFrameRate) && taken;
        });
        QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("frame %1: %2").arg(i).arg(error)));
        QVERIFY(taken);
    }
    const QString finishError = finish(*encoder);
    QVERIFY2(finishError.isEmpty(), qPrintable(finishError));
    QCOMPARE(encoder->framesWritten(), qint64(kFrameCount));
    const QString colors = compareBars(decodeMiddleFrame(path));
    QVERIFY2(colors.isEmpty(), qPrintable(colors));
}

void tst_AVFoundationNativeFrames::imagePathStillEncodesColorBars()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("image.mp4"));
    auto encoder = startEncoder(path);
    QVERIFY(encoder);
    const QImage bars = colorBars();
    for (int i = 0; i < kFrameCount; ++i) {
        const QString error = writeUntilAccepted(*encoder, [&] {
            encoder->writeFrame(bars, qint64(i) * 1000 / kFrameRate);
        });
        QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("frame %1: %2").arg(i).arg(error)));
    }
    const QString finishError = finish(*encoder);
    QVERIFY2(finishError.isEmpty(), qPrintable(finishError));
    const QString colors = compareBars(decodeMiddleFrame(path));
    QVERIFY2(colors.isEmpty(), qPrintable(colors));
}

void tst_AVFoundationNativeFrames::mismatchedSurfaceIsDeclined_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<uint>("format");
    QTest::newRow("other size") << QSize(320, 180) << uint(kCVPixelFormatType_32BGRA);
    QTest::newRow("odd size") << QSize(641, 361) << uint(kCVPixelFormatType_32BGRA);
    QTest::newRow("yuv") << kFrameSize << uint(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
}

void tst_AVFoundationNativeFrames::mismatchedSurfaceIsDeclined()
{
    QFETCH(QSize, size);
    QFETCH(uint, format);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto encoder = startEncoder(dir.filePath(QStringLiteral("declined.mp4")));
    QVERIFY(encoder);
    CVPixelBufferRef buffer = nullptr;
    NSDictionary* attributes = @{(NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    QCOMPARE(CVPixelBufferCreate(kCFAllocatorDefault, size.width(), size.height(), format,
                                 (__bridge CFDictionaryRef)attributes, &buffer),
             kCVReturnSuccess);
    const auto surface = adoptPixelBuffer(buffer);
    // Declined, so the caller writes the image instead; nothing was counted.
    QVERIFY(!encoder->writeNativeFrame(surface, 0));
    QCOMPARE(encoder->framesWritten(), qint64(0));
    QVERIFY(!encoder->writeNativeFrame(nullptr, 0));
    // A Core Foundation object that is not a pixel buffer.
    const std::shared_ptr<void> notPixels(const_cast<void*>(static_cast<const void*>(CFSTR("frame"))), [](void*) {});
    QVERIFY(!encoder->writeNativeFrame(notPixels, 0));
    QCOMPARE(encoder->framesWritten(), qint64(0));
    encoder->abort();
}

QTEST_MAIN(tst_AVFoundationNativeFrames)
#include "tst_AVFoundationNativeFrames_mac.moc"
