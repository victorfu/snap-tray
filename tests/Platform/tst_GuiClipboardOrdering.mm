#include <QtTest/QtTest>

#include <QImage>
#include <QClipboard>
#include <QColorSpace>
#include <QBuffer>

#include "PlatformFeatures.h"

#import <AppKit/AppKit.h>

namespace {

QImage makeHighEntropyImage(const QSize& size)
{
    QImage image(size, QImage::Format_ARGB32);
    quint32 state = 0x6d2b79f5U;

    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            line[x] = qRgba(state & 0xffU, (state >> 8) & 0xffU, (state >> 16) & 0xffU, 0xffU);
        }
    }

    return image;
}

QImage makeLatestImage()
{
    QImage image(QSize(29, 17), QImage::Format_ARGB32);
    image.fill(QColor(17, 193, 126));
    return image;
}

} // namespace

class tst_GuiClipboardOrdering : public QObject
{
    Q_OBJECT

private slots:
    void latestQueuedGuiCopyWins();
    void colorSpaceTaggedImageCanFulfillNativePasteboardData_data();
    void colorSpaceTaggedImageCanFulfillNativePasteboardData();
};

void tst_GuiClipboardOrdering::latestQueuedGuiCopyWins()
{
    QImage sentinelImage(QSize(7, 5), QImage::Format_ARGB32);
    sentinelImage.fill(QColor(201, 37, 91));
    QVERIFY(PlatformFeatures::instance().copyImageToClipboardForGui(sentinelImage));
    QCOMPARE(QGuiApplication::clipboard()->image().size(), sentinelImage.size());

    const QImage staleImage = makeHighEntropyImage(QSize(4096, 3072));
    const QImage latestImage = makeLatestImage();

    bool staleCompletionCalled = false;
    bool staleCopySucceeded = false;
    bool latestCompletionCalled = false;
    bool latestCopySucceeded = false;
    QImage pasteboardImageAtLatestCompletion;

    PlatformFeatures::instance().copyImageToClipboardForGuiAsync(
        staleImage,
        qApp,
        [&staleCompletionCalled, &staleCopySucceeded](
            PlatformFeatures::ClipboardCopyResult result) {
            staleCompletionCalled = true;
            staleCopySucceeded =
                result == PlatformFeatures::ClipboardCopyResult::Success;
        });

    PlatformFeatures::instance().copyImageToClipboardForGuiAsync(
        latestImage,
        qApp,
        [&latestCompletionCalled, &latestCopySucceeded,
            &pasteboardImageAtLatestCompletion](
            PlatformFeatures::ClipboardCopyResult result) {
            latestCompletionCalled = true;
            latestCopySucceeded = result == PlatformFeatures::ClipboardCopyResult::Success;
            pasteboardImageAtLatestCompletion = QGuiApplication::clipboard()->image();
        });

    QTRY_VERIFY_WITH_TIMEOUT(latestCompletionCalled, 5000);
    QVERIFY(latestCopySucceeded);
    QCOMPARE(pasteboardImageAtLatestCompletion.size(), latestImage.size());
    QCOMPARE(pasteboardImageAtLatestCompletion.pixelColor(0, 0), latestImage.pixelColor(0, 0));

    QTRY_VERIFY_WITH_TIMEOUT(staleCompletionCalled, 15000);
    QVERIFY(staleCopySucceeded);

    const QImage finalImage = QGuiApplication::clipboard()->image();
    QCOMPARE(finalImage.size(), latestImage.size());
    QCOMPARE(finalImage.pixelColor(0, 0), latestImage.pixelColor(0, 0));
}

void tst_GuiClipboardOrdering::colorSpaceTaggedImageCanFulfillNativePasteboardData_data()
{
    QTest::addColumn<QColorSpace>("colorSpace");
    QTest::addColumn<QSize>("size");
    const auto addSizes = [](const QByteArray& name, const QColorSpace& space) {
        QTest::newRow((name + "-small").constData()) << space << QSize(29, 17);
        QTest::newRow((name + "-large").constData()) << space << QSize(4096, 3072);
    };
    addSizes("sRGB", QColorSpace(QColorSpace::SRgb));
    addSizes("DisplayP3", QColorSpace(QColorSpace::DisplayP3));
    @autoreleasepool {
        int index = 0;
        for (NSScreen* screen in [NSScreen screens]) {
            NSData* profile = screen.colorSpace.ICCProfileData;
            const QColorSpace space = QColorSpace::fromIccProfile(QByteArray(
                static_cast<const char*>(profile.bytes), static_cast<int>(profile.length)));
            QVERIFY2(space.isValid(), "Screen ICC profile must be valid");
            qInfo() << "Screen" << index << QString::fromNSString(screen.localizedName)
                    << "ICC:" << space.description();
            addSizes("screenICC-" + QByteArray::number(index++), space);
        }
    }
}

void tst_GuiClipboardOrdering::colorSpaceTaggedImageCanFulfillNativePasteboardData()
{
    QFETCH(QColorSpace, colorSpace);
    QFETCH(QSize, size);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(80, 160, 120));
    image.setPixelColor(0, 0, QColor(0, 0, 0, 0));
    image.setPixelColor(1, 0, QColor(80, 160, 120, 128));
    image.setColorSpace(colorSpace);
    QVERIFY(PlatformFeatures::instance().copyImageToClipboardForGui(image));

    // Exercise native promised image data, where Qt's macOS color-space
    // conversion previously trapped; a Qt image round trip alone misses it.
    @autoreleasepool {
        NSData* data = [[NSPasteboard generalPasteboard] dataForType:NSPasteboardTypeTIFF];
        QVERIFY(data != nil);
        QVERIFY(data.length > 0);
        NSBitmapImageRep* decoded = [NSBitmapImageRep imageRepWithData:data];
        QVERIFY(decoded != nil);
        QCOMPARE(decoded.pixelsWide, image.width());
        QCOMPARE(decoded.pixelsHigh, image.height());
        QVERIFY(decoded.hasAlpha);
        // Use the same native color-management engine for the PNG reference
        // and TIFF consumer, avoiding differences between Qt and ColorSync.
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        NSBitmapImageRep* referenceImage = [NSBitmapImageRep imageRepWithData:
            [NSData dataWithBytes:png.constData() length:png.size()]];
        QVERIFY(referenceImage != nil);
        NSBitmapImageRep* actualSRgb = [decoded bitmapImageRepByConvertingToColorSpace:
            [NSColorSpace sRGBColorSpace] renderingIntent:NSColorRenderingIntentDefault];
        NSBitmapImageRep* referenceSRgb = [referenceImage bitmapImageRepByConvertingToColorSpace:
            [NSColorSpace sRGBColorSpace] renderingIntent:NSColorRenderingIntentDefault];
        QVERIFY(actualSRgb != nil);
        QVERIFY(referenceSRgb != nil);
        constexpr int channelTolerance = 3;
        bool colorsMatch = true;
        qInfo() << "Decoded TIFF color space:" << QString::fromNSString(decoded.colorSpace.localizedName);
        for (int x = 0; x < 3; ++x) {
            NSColor* pixel = [actualSRgb colorAtX:x y:0];
            QVERIFY(pixel != nil);
            NSColor* referencePixel = [referenceSRgb colorAtX:x y:0];
            QVERIFY(referencePixel != nil);
            const QColor reference = QColor::fromRgbF(referencePixel.redComponent,
                referencePixel.greenComponent, referencePixel.blueComponent, referencePixel.alphaComponent);
            qInfo() << "pixel" << x << "actual" << pixel.redComponent << pixel.greenComponent
                    << pixel.blueComponent << pixel.alphaComponent << "expected" << reference
                    << "bitmapFormat" << decoded.bitmapFormat;
            QVERIFY(qAbs(qRound(pixel.alphaComponent * 255) - reference.alpha()) <= 1);
            if (reference.alpha() != 0) {
                colorsMatch &= qAbs(qRound(pixel.redComponent * 255) - reference.red()) <= channelTolerance;
                colorsMatch &= qAbs(qRound(pixel.greenComponent * 255) - reference.green()) <= channelTolerance;
                colorsMatch &= qAbs(qRound(pixel.blueComponent * 255) - reference.blue()) <= channelTolerance;
            }
        }
        QVERIFY2(colorsMatch, "Native TIFF colors differ after conversion to sRGB");
    }
}

QTEST_MAIN(tst_GuiClipboardOrdering)
#include "tst_GuiClipboardOrdering.moc"
