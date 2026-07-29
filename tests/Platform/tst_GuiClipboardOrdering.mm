#include <QtTest/QtTest>

#include <QImage>

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

QImage readPngImageFromGeneralPasteboard()
{
    // PlatformFeatures writes NSPasteboard directly. QClipboard observes that
    // native change asynchronously, so reading it in the completion callback
    // can return stale cached data even though the pasteboard write is done.
    @autoreleasepool {
        NSData* pngData = [[NSPasteboard generalPasteboard] dataForType:NSPasteboardTypePNG];
        if (!pngData) {
            return {};
        }

        return QImage::fromData(
            static_cast<const uchar*>(pngData.bytes),
            static_cast<int>(pngData.length),
            "PNG");
    }
}

} // namespace

class tst_GuiClipboardOrdering : public QObject
{
    Q_OBJECT

private slots:
    void latestGuiCopyWinsWhenEncodingCompletesOutOfOrder();
};

void tst_GuiClipboardOrdering::latestGuiCopyWinsWhenEncodingCompletesOutOfOrder()
{
    QImage sentinelImage(QSize(7, 5), QImage::Format_ARGB32);
    sentinelImage.fill(QColor(201, 37, 91));
    QVERIFY(PlatformFeatures::instance().copyImageToClipboardForGui(sentinelImage));
    QCOMPARE(readPngImageFromGeneralPasteboard().size(), sentinelImage.size());

    const QImage staleImage = makeHighEntropyImage(QSize(4096, 3072));
    const QImage latestImage = makeLatestImage();

    bool staleCompletionCalled = false;
    bool staleCopyWasSuperseded = false;
    bool latestCompletionCalled = false;
    bool latestCopySucceeded = false;
    QImage pasteboardImageAtLatestCompletion;

    PlatformFeatures::instance().copyImageToClipboardForGuiAsync(
        staleImage,
        qApp,
        [&staleCompletionCalled, &staleCopyWasSuperseded](
            PlatformFeatures::ClipboardCopyResult result) {
            staleCompletionCalled = true;
            staleCopyWasSuperseded =
                result == PlatformFeatures::ClipboardCopyResult::Superseded;
        });

    PlatformFeatures::instance().copyImageToClipboardForGuiAsync(
        latestImage,
        qApp,
        [&latestCompletionCalled, &latestCopySucceeded,
            &pasteboardImageAtLatestCompletion](
            PlatformFeatures::ClipboardCopyResult result) {
            latestCompletionCalled = true;
            latestCopySucceeded = result == PlatformFeatures::ClipboardCopyResult::Success;
            pasteboardImageAtLatestCompletion = readPngImageFromGeneralPasteboard();
        });

    QTRY_VERIFY_WITH_TIMEOUT(latestCompletionCalled, 5000);
    QVERIFY(latestCopySucceeded);
    QCOMPARE(pasteboardImageAtLatestCompletion.size(), latestImage.size());
    QCOMPARE(pasteboardImageAtLatestCompletion.pixelColor(0, 0), latestImage.pixelColor(0, 0));

    QTRY_VERIFY_WITH_TIMEOUT(staleCompletionCalled, 15000);
    QVERIFY(staleCopyWasSuperseded);

    const QImage finalImage = readPngImageFromGeneralPasteboard();
    QCOMPARE(finalImage.size(), latestImage.size());
    QCOMPARE(finalImage.pixelColor(0, 0), latestImage.pixelColor(0, 0));
}

QTEST_MAIN(tst_GuiClipboardOrdering)
#include "tst_GuiClipboardOrdering.moc"
