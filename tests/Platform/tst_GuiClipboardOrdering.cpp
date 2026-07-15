#include <QtTest/QtTest>

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>

#include "PlatformFeatures.h"

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
    void latestGuiCopyWinsWhenEncodingCompletesOutOfOrder();
};

void tst_GuiClipboardOrdering::latestGuiCopyWinsWhenEncodingCompletesOutOfOrder()
{
#ifndef Q_OS_MACOS
    QSKIP("GUI clipboard encoding is asynchronous only on macOS.");
#else
    QClipboard* clipboard = QGuiApplication::clipboard();
    QVERIFY(clipboard);

    QImage sentinelImage(QSize(7, 5), QImage::Format_ARGB32);
    sentinelImage.fill(QColor(201, 37, 91));
    QVERIFY(PlatformFeatures::instance().copyImageToClipboardForGui(sentinelImage));
    QCOMPARE(clipboard->image().size(), sentinelImage.size());

    const QImage staleImage = makeHighEntropyImage(QSize(4096, 3072));
    const QImage latestImage = makeLatestImage();

    bool staleCompletionCalled = false;
    bool staleCopyWasSuperseded = false;
    bool latestCompletionCalled = false;
    bool latestCopySucceeded = false;
    QImage clipboardImageAtLatestCompletion;

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
        [clipboard, &latestCompletionCalled, &latestCopySucceeded,
            &clipboardImageAtLatestCompletion](
            PlatformFeatures::ClipboardCopyResult result) {
            latestCompletionCalled = true;
            latestCopySucceeded = result == PlatformFeatures::ClipboardCopyResult::Success;
            clipboardImageAtLatestCompletion = clipboard->image();
        });

    QTRY_VERIFY_WITH_TIMEOUT(latestCompletionCalled, 5000);
    QVERIFY(latestCopySucceeded);
    QCOMPARE(clipboardImageAtLatestCompletion.size(), latestImage.size());
    QCOMPARE(clipboardImageAtLatestCompletion.pixelColor(0, 0), latestImage.pixelColor(0, 0));

    QTRY_VERIFY_WITH_TIMEOUT(staleCompletionCalled, 15000);
    QVERIFY(staleCopyWasSuperseded);

    const QImage finalImage = clipboard->image();
    QCOMPARE(finalImage.size(), latestImage.size());
    QCOMPARE(finalImage.pixelColor(0, 0), latestImage.pixelColor(0, 0));
#endif
}

QTEST_MAIN(tst_GuiClipboardOrdering)
#include "tst_GuiClipboardOrdering.moc"
