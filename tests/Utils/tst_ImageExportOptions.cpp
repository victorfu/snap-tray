#include <QtTest>

#include <QBuffer>
#include <QColorSpace>
#include <QImageReader>
#include <QPainter>

#include "utils/ImageExportOptions.h"

using Resize = ImageExportOptions::Resize;
using Format = ImageExportOptions::Format;

namespace {

// Text, thin lines and a gradient: content where JPEG quality matters.
QImage sampleImage(const QSize& size, bool transparentCorner = false)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QLinearGradient gradient(0, 0, size.width(), size.height());
    gradient.setColorAt(0, QColor(30, 90, 200));
    gradient.setColorAt(1, QColor(240, 180, 40));
    painter.fillRect(QRect(0, 0, size.width(), size.height() / 2), gradient);
    painter.setPen(Qt::black);
    for (int x = 0; x < size.width(); x += 7) {
        painter.drawLine(x, size.height() / 2, x + 3, size.height());
    }
    painter.drawText(QRect(QPoint(4, 4), size), QStringLiteral("SnapTray export 12345"));
    painter.end();
    if (transparentCorner) {
        for (int y = 0; y < qMin(8, size.height()); ++y) {
            for (int x = 0; x < qMin(8, size.width()); ++x) {
                image.setPixelColor(x, y, QColor(0, 0, 0, 0));
            }
        }
    }
    return image;
}

QImage decode(const QByteArray& data, QByteArray* format = nullptr)
{
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    if (format) {
        *format = reader.format();
    }
    return reader.read();
}

} // namespace

class tst_ImageExportOptions : public QObject
{
    Q_OBJECT

private slots:
    void outputSize_data();
    void outputSize();
    void outputSizeIsInPixelsWhateverTheDevicePixelRatio();
    void normalizedClampsEveryValue();
    void formatNamesAndSuffixes();
    void renderLeavesSourceUntouched();
    void renderOriginalSizeSharesThePixels();
    void renderKeepsColorSpace();
    void pngIsLosslessAtOutputSize();
    void jpegQualityReachesTheEncoder();
    void jpegQualityIsIgnoredForPng();
    void encodedSizeIsTheByteCount();
    void transparencyIsReportedOnlyWhenJpegDropsIt();
    void jpegFillsTransparencyWithWhiteForEveryPixelFormat();
    void hasTransparencyScansPixels();
    void veryLongImageKeepsItsShape();
    void nullImageFails();
};

void tst_ImageExportOptions::outputSize_data()
{
    QTest::addColumn<int>("resize");
    QTest::addColumn<int>("value");
    QTest::addColumn<QSize>("source");
    QTest::addColumn<QSize>("expected");

    QTest::newRow("original") << int(Resize::Original) << 0 << QSize(2560, 1440) << QSize(2560, 1440);
    QTest::newRow("50 percent") << int(Resize::Percent) << 50 << QSize(2560, 1440) << QSize(1280, 720);
    QTest::newRow("25 percent of odd size") << int(Resize::Percent) << 25 << QSize(1001, 333) << QSize(250, 83);
    QTest::newRow("100 percent") << int(Resize::Percent) << 100 << QSize(801, 601) << QSize(801, 601);
    QTest::newRow("percent never below one pixel") << int(Resize::Percent) << 5 << QSize(9, 3) << QSize(1, 1);
    QTest::newRow("long edge landscape") << int(Resize::LongEdge) << 1280 << QSize(2560, 1440) << QSize(1280, 720);
    QTest::newRow("long edge portrait") << int(Resize::LongEdge) << 1000 << QSize(1500, 3000) << QSize(500, 1000);
    QTest::newRow("long edge never enlarges") << int(Resize::LongEdge) << 4000 << QSize(2560, 1440) << QSize(2560, 1440);
    QTest::newRow("long edge equal to source") << int(Resize::LongEdge) << 2560 << QSize(2560, 1440) << QSize(2560, 1440);
    QTest::newRow("very long image keeps a short side") << int(Resize::LongEdge) << 1000 << QSize(300, 30000) << QSize(10, 1000);
    QTest::newRow("extreme ratio keeps one pixel") << int(Resize::LongEdge) << 16 << QSize(2, 16000) << QSize(1, 16);
}

void tst_ImageExportOptions::outputSize()
{
    QFETCH(int, resize);
    QFETCH(int, value);
    QFETCH(QSize, source);
    QFETCH(QSize, expected);

    ImageExportOptions options;
    options.resize = static_cast<Resize>(resize);
    options.percent = value;
    options.longEdge = value;
    const QSize output = options.outputSize(source);
    QCOMPARE(output, expected);
    QVERIFY(output.width() <= source.width());
    QVERIFY(output.height() <= source.height());

    // The encoded file really has that many pixels.
    if (qint64(source.width()) * source.height() <= 4000000) {
        const ImageExport::Encoded encoded = ImageExport::encode(sampleImage(source), options);
        QVERIFY2(encoded.isValid(), qPrintable(encoded.error));
        QCOMPARE(encoded.pixelSize, expected);
        QCOMPARE(decode(encoded.data).size(), expected);
    }
}

void tst_ImageExportOptions::outputSizeIsInPixelsWhateverTheDevicePixelRatio()
{
    ImageExportOptions options;
    options.resize = Resize::LongEdge;
    options.longEdge = 600;

    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0}) {
        // 1200 x 800 pixels: 600 x 400 points at 2x, 960 x 640 at 1.25x.
        QImage image = sampleImage(QSize(1200, 800));
        image.setDevicePixelRatio(dpr);
        const ImageExport::Encoded encoded = ImageExport::encode(image, options);
        QVERIFY(encoded.isValid());
        QCOMPARE(encoded.pixelSize, QSize(600, 400));
        const QImage decoded = decode(encoded.data);
        QCOMPARE(decoded.size(), QSize(600, 400));
    }
}

void tst_ImageExportOptions::normalizedClampsEveryValue()
{
    ImageExportOptions options;
    options.resize = static_cast<Resize>(42);
    options.format = static_cast<Format>(-3);
    options.percent = 500;
    options.longEdge = 1;
    options.jpegQuality = 0;
    const ImageExportOptions normalized = options.normalized();
    QCOMPARE(normalized.resize, Resize::Original);
    QCOMPARE(normalized.format, Format::Png);
    QCOMPARE(normalized.percent, ImageExportOptions::kMaxPercent);
    QCOMPARE(normalized.longEdge, ImageExportOptions::kMinLongEdge);
    QCOMPARE(normalized.jpegQuality, ImageExportOptions::kMinJpegQuality);
    QCOMPARE(ImageExportOptions().normalized(), ImageExportOptions());
}

void tst_ImageExportOptions::formatNamesAndSuffixes()
{
    ImageExportOptions png;
    QCOMPARE(png.formatName(), QByteArray("png"));
    QCOMPARE(png.suffix(), QStringLiteral("png"));
    QVERIFY(!png.isLossy());
    QCOMPARE(png.acceptedSuffixes(), QStringList{QStringLiteral("png")});

    ImageExportOptions jpeg;
    jpeg.format = Format::Jpeg;
    QCOMPARE(jpeg.formatName(), QByteArray("jpeg"));
    QCOMPARE(jpeg.suffix(), QStringLiteral("jpg"));
    QVERIFY(jpeg.isLossy());
    QVERIFY(jpeg.acceptedSuffixes().contains(QStringLiteral("jpeg")));
}

void tst_ImageExportOptions::renderLeavesSourceUntouched()
{
    const QImage source = sampleImage(QSize(400, 300), true);
    const QImage before = source.copy();
    const qint64 key = source.cacheKey();

    ImageExportOptions options;
    options.resize = Resize::Percent;
    options.percent = 25;
    options.format = Format::Jpeg;
    options.jpegQuality = 20;
    const QImage rendered = ImageExport::render(source, options);
    const ImageExport::Encoded encoded = ImageExport::encode(source, options);

    QCOMPARE(rendered.size(), QSize(100, 75));
    QVERIFY(encoded.isValid());
    QCOMPARE(source, before);
    QCOMPARE(source.cacheKey(), key);
    QCOMPARE(source.size(), QSize(400, 300));
}

void tst_ImageExportOptions::renderOriginalSizeSharesThePixels()
{
    const QImage source = sampleImage(QSize(120, 80));
    const QImage rendered = ImageExport::render(source, ImageExportOptions());
    QCOMPARE(rendered.cacheKey(), source.cacheKey());
}

void tst_ImageExportOptions::renderKeepsColorSpace()
{
    QImage source = sampleImage(QSize(200, 100));
    source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    ImageExportOptions options;
    options.resize = Resize::Percent;
    options.percent = 50;
    const QImage rendered = ImageExport::render(source, options);
    QCOMPARE(rendered.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
    QCOMPARE(rendered.devicePixelRatio(), 1.0);
}

void tst_ImageExportOptions::pngIsLosslessAtOutputSize()
{
    const QImage source = sampleImage(QSize(321, 123), true);

    // At original size every pixel, alpha included, survives.
    const ImageExport::Encoded original = ImageExport::encode(source, ImageExportOptions());
    QVERIFY(original.isValid());
    QByteArray format;
    const QImage decoded = decode(original.data, &format);
    QCOMPARE(format, QByteArray("png"));
    QCOMPARE(decoded.convertToFormat(QImage::Format_ARGB32), source.convertToFormat(QImage::Format_ARGB32));
    QVERIFY(!original.transparencyDropped);

    // Resized: the file holds exactly the pixels render() produced.
    ImageExportOptions half;
    half.resize = Resize::Percent;
    half.percent = 50;
    const ImageExport::Encoded encoded = ImageExport::encode(source, half);
    QCOMPARE(decode(encoded.data).convertToFormat(QImage::Format_ARGB32),
             ImageExport::render(source, half).convertToFormat(QImage::Format_ARGB32));
}

void tst_ImageExportOptions::jpegQualityReachesTheEncoder()
{
    const QImage source = sampleImage(QSize(640, 480));
    ImageExportOptions options;
    options.format = Format::Jpeg;

    qint64 previous = 0;
    for (const int quality : {10, 50, 90, 100}) {
        options.jpegQuality = quality;
        const ImageExport::Encoded encoded = ImageExport::encode(source, options);
        QVERIFY2(encoded.isValid(), qPrintable(encoded.error));
        QByteArray format;
        QVERIFY(!decode(encoded.data, &format).isNull());
        QCOMPARE(format, QByteArray("jpeg"));
        QVERIFY2(encoded.data.size() > previous,
                 qPrintable(QStringLiteral("quality %1 gave %2 bytes, not more than %3")
                                .arg(quality).arg(encoded.data.size()).arg(previous)));
        previous = encoded.data.size();
    }

    // Lower quality really loses detail: it is not the same image.
    options.jpegQuality = 10;
    const QImage low = decode(ImageExport::encode(source, options).data).convertToFormat(QImage::Format_RGB32);
    QVERIFY(low != source.convertToFormat(QImage::Format_RGB32));
}

void tst_ImageExportOptions::jpegQualityIsIgnoredForPng()
{
    const QImage source = sampleImage(QSize(320, 240));
    ImageExportOptions a;
    a.jpegQuality = 10;
    ImageExportOptions b;
    b.jpegQuality = 100;
    QCOMPARE(ImageExport::encode(source, a).data, ImageExport::encode(source, b).data);
}

void tst_ImageExportOptions::encodedSizeIsTheByteCount()
{
    const QImage source = sampleImage(QSize(500, 400));
    ImageExportOptions options;
    options.format = Format::Jpeg;
    options.resize = Resize::LongEdge;
    options.longEdge = 250;
    const ImageExport::Encoded encoded = ImageExport::encode(source, options);
    QVERIFY(encoded.isValid());
    QCOMPARE(encoded.options, options);
    // Encoding the same input again gives the same file, so the size shown
    // for a candidate is the size of what gets written.
    QCOMPARE(ImageExport::encode(source, options).data, encoded.data);
    QVERIFY(encoded.data.size() > 0);
}

void tst_ImageExportOptions::transparencyIsReportedOnlyWhenJpegDropsIt()
{
    ImageExportOptions jpeg;
    jpeg.format = Format::Jpeg;

    QVERIFY(ImageExport::encode(sampleImage(QSize(64, 64), true), jpeg).transparencyDropped);
    QVERIFY(!ImageExport::encode(sampleImage(QSize(64, 64), false), jpeg).transparencyDropped);
    QVERIFY(!ImageExport::encode(sampleImage(QSize(64, 64), true), ImageExportOptions()).transparencyDropped);
}

// Left to the JPEG writer, a see-through pixel became black, or kept its
// hidden color, depending on the image format. The export decides instead.
void tst_ImageExportOptions::jpegFillsTransparencyWithWhiteForEveryPixelFormat()
{
    ImageExportOptions jpeg;
    jpeg.format = Format::Jpeg;
    jpeg.jpegQuality = 100;

    for (const QImage::Format format :
         {QImage::Format_ARGB32, QImage::Format_ARGB32_Premultiplied, QImage::Format_RGBA8888}) {
        QImage source(64, 64, QImage::Format_ARGB32);
        source.fill(QColor(0, 0, 0, 0));                 // fully transparent
        for (int y = 0; y < 64; ++y) {
            for (int x = 32; x < 64; ++x) {
                source.setPixelColor(x, y, QColor(255, 0, 0, 64));   // faint red
            }
        }
        source = source.convertToFormat(format);
        const QImage sourceBefore = source.copy();

        const ImageExport::Encoded encoded = ImageExport::encode(source, jpeg);
        QVERIFY2(encoded.isValid(), qPrintable(encoded.error));
        QVERIFY(encoded.transparencyDropped);
        const QImage decoded = decode(encoded.data);
        const QColor clear = decoded.pixelColor(8, 32);
        QVERIFY2(clear.red() > 245 && clear.green() > 245 && clear.blue() > 245,
                 qPrintable(QStringLiteral("format %1: %2").arg(format).arg(clear.name())));
        // 25% red over white, not over black and not full red.
        const QColor faint = decoded.pixelColor(52, 32);
        QVERIFY2(faint.red() > 240 && faint.green() > 170 && faint.green() < 215,
                 qPrintable(QStringLiteral("format %1: %2").arg(format).arg(faint.name())));
        QCOMPARE(source, sourceBefore);
    }

    // PNG keeps the alpha as it is.
    QImage clearImage(8, 8, QImage::Format_ARGB32);
    clearImage.fill(QColor(0, 0, 0, 0));
    QCOMPARE(decode(ImageExport::encode(clearImage, ImageExportOptions()).data).pixelColor(1, 1).alpha(), 0);
}

void tst_ImageExportOptions::hasTransparencyScansPixels()
{
    QVERIFY(!ImageExport::hasTransparency(QImage()));
    QImage opaqueRgb(8, 8, QImage::Format_RGB32);
    opaqueRgb.fill(Qt::red);
    QVERIFY(!ImageExport::hasTransparency(opaqueRgb));
    // An alpha channel that is fully opaque is not transparency.
    QVERIFY(!ImageExport::hasTransparency(sampleImage(QSize(32, 32), false)));
    QVERIFY(ImageExport::hasTransparency(sampleImage(QSize(32, 32), true)));
    QImage straight(4, 4, QImage::Format_RGBA8888);
    straight.fill(QColor(10, 20, 30, 254));
    QVERIFY(ImageExport::hasTransparency(straight));
}

void tst_ImageExportOptions::veryLongImageKeepsItsShape()
{
    // A scrolling capture: narrow and very tall.
    const QImage source = sampleImage(QSize(240, 12000));
    ImageExportOptions options;
    options.resize = Resize::LongEdge;
    options.longEdge = 3000;
    const ImageExport::Encoded encoded = ImageExport::encode(source, options);
    QVERIFY2(encoded.isValid(), qPrintable(encoded.error));
    QCOMPARE(encoded.pixelSize, QSize(60, 3000));
    QCOMPARE(decode(encoded.data).size(), QSize(60, 3000));
}

void tst_ImageExportOptions::nullImageFails()
{
    const ImageExport::Encoded encoded = ImageExport::encode(QImage(), ImageExportOptions());
    QVERIFY(!encoded.isValid());
    QVERIFY(!encoded.error.isEmpty());
    QVERIFY(encoded.data.isEmpty());
    QVERIFY(ImageExport::render(QImage(), ImageExportOptions()).isNull());
    QVERIFY(!ImageExportOptions().outputSize(QSize()).isValid());
}

QTEST_MAIN(tst_ImageExportOptions)
#include "tst_ImageExportOptions.moc"
