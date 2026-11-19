#include <QtTest/QtTest>
#include <QPainter>
#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QColorSpace>
#include <QElapsedTimer>
#include "beautify/BeautifyBackgroundImage.h"
#include "beautify/BeautifyRenderer.h"
#include "beautify/BeautifySettings.h"

class tst_BeautifyRenderer : public QObject
{
    Q_OBJECT

private slots:
    // calculateOutputSize tests
    void testCalculateOutputSize_DefaultPadding();
    void testCalculateOutputSize_CustomPadding();
    void testCalculateOutputSize_WithAspectRatio_Auto();
    void testCalculateOutputSize_WithAspectRatio_Square();
    void testCalculateOutputSize_WithAspectRatio_Wide16_9();
    void testCalculateOutputSize_WithAspectRatio_Tall9_16();

    // applyToPixmap tests
    void testApplyToPixmap_NullSource();
    void testApplyToPixmap_ProducesNonNullResult();
    void testApplyToPixmap_OutputSizeMatchesCalculated();
    void testApplyToPixmap_SolidBackground();
    void testApplyToPixmap_LinearGradient();
    void testApplyToPixmap_RadialGradient();
    void testApplyToPixmap_NoShadow();
    void testApplyToPixmap_WithShadow();
    void testApplyToPixmap_ZeroCornerRadius();
    void testApplyToPixmap_HiDPI_CorrectLogicalSize();
    void testApplyToPixmap_BorderInsideThePicture();

    // applyToImage (#353) must draw exactly what applyToPixmap draws
    void testApplyToImage_NullSource();
    void testApplyToImage_MatchesApplyToPixmap_data();
    void testApplyToImage_MatchesApplyToPixmap();

    void transparentCanvas();
    void previewMatchesOutputAtEveryAnchor();
    void orientedBackgroundPreviewAndFullDecode();
    void thumbnailCacheBudget();
    void pngThumbnailAndDecodeLimit();

    // render tests
    void testRender_NullSource_NoOp();
    void testRender_FitsTargetRect();

private:
    QPixmap createTestPixmap(int w, int h, QColor fill = Qt::red);
};

void tst_BeautifyRenderer::transparentCanvas()
{
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::Transparent;
    settings.shadowEnabled = false;
    const auto source = createTestPixmap(16, 16);
    const auto result = BeautifyRenderer::applyToImage(source.toImage(), settings);
    QCOMPARE(result.pixelColor(0, 0).alpha(), 0);
}

void tst_BeautifyRenderer::previewMatchesOutputAtEveryAnchor()
{
    BeautifySettings s;
    s.padding = 8;
    s.cornerRadius = 0;
    s.shadowEnabled = false;
    s.backgroundType = BeautifyBackgroundType::Transparent;
    for (QSize size : {QSize(144, 12), QSize(12, 144)}) {
        QPixmap source = createTestPixmap(size.width(), size.height());
        source.setDevicePixelRatio(1.5);
        const QSize logical = source.deviceIndependentSize().toSize();
        for (int ratio = 0; ratio <= int(BeautifyAspectRatio::Twitter_2_1); ++ratio) {
            s.aspectRatio = BeautifyAspectRatio(ratio);
            const auto canvas = BeautifyRenderer::calculateOutputSize(logical, s);
            for (int anchor = 0; anchor <= int(BeautifyAlignment::BottomRight); ++anchor) {
                s.alignment = BeautifyAlignment(anchor);
                const auto inset = BeautifyRenderer::calculateInsetRect(canvas, logical, s);
                const QPoint topLeft(s.padding, s.padding);
                const QPoint bottomRight(canvas.width() - s.padding - logical.width(),
                                         canvas.height() - s.padding - logical.height());
                QCOMPARE(inset.x(), topLeft.x() + (bottomRight.x() - topLeft.x()) * (anchor % 3) / 2);
                QCOMPARE(inset.y(), topLeft.y() + (bottomRight.y() - topLeft.y()) * (anchor / 3) / 2);
                auto out = BeautifyRenderer::applyToImage(source.toImage(), s);
                QImage preview(out.size(), out.format());
                preview.setDevicePixelRatio(1.5);
                preview.fill(Qt::transparent);
                QPainter painter(&preview);
                BeautifyRenderer::render(painter, QRect(QPoint(), canvas), source, s);
                painter.end();
                QCOMPARE(preview, out);
            }
        }
    }
}

void tst_BeautifyRenderer::orientedBackgroundPreviewAndFullDecode()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage src(3072, 768, QImage::Format_RGB32);
    src.fill(QColor(201, 88, 41));
    src.setColorSpace(QColorSpace::DisplayP3);
    const QString path = dir.filePath("oriented.jpg");
    // EXIF orientation 6 rotates the stored pixels 90 degrees clockwise.
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(src.save(&buffer, "JPEG", 100));
    jpeg.insert(2, QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(jpeg), jpeg.size());
    file.close();
    QImageReader expectedReader(path);
    QVERIFY(expectedReader.transformation().testFlag(QImageIOHandler::TransformationRotate90));
    const auto expected = expectedReader.read().transformed(QTransform().rotate(90));
    QElapsedTimer timer;
    timer.start();
    const auto preview = SnapTray::BeautifyBackgroundImage::loadPreview(path);
    const auto previewMs = timer.restart();
    const auto full = SnapTray::BeautifyBackgroundImage::decode(preview.original);
    QCOMPARE(full, expected);
    QCOMPARE(full.size(), QSize(768, 3072));
    QCOMPARE(preview.thumbnail.size(), expected.size().scaled(1024, 1024, Qt::KeepAspectRatio));
    QVERIFY(preview.thumbnail.sizeInBytes() < full.sizeInBytes());
    qInfo() << "thumbnail ms, full ms, thumbnail bytes, full bytes, cached bytes"
            << previewMs << timer.elapsed() << preview.thumbnail.sizeInBytes() << full.sizeInBytes()
            << SnapTray::BeautifyBackgroundImage::cachedBytes();
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::Image;
    settings.backgroundImageData = preview.original;
    settings.shadowEnabled = false;
    QImage screenshot(48, 32, QImage::Format_RGB32);
    screenshot.fill(Qt::white);
    screenshot.setColorSpace(QColorSpace::SRgb);
    const auto rendered = BeautifyRenderer::applyToImage(screenshot, settings);
    QCOMPARE(rendered.pixelColor(0, 0), full.convertedToColorSpace(QColorSpace::SRgb).pixelColor(0, 0));
    settings.backgroundImageData.clear();
    QVERIFY(BeautifyRenderer::applyToImage(screenshot, settings).isNull());
}

void tst_BeautifyRenderer::thumbnailCacheBudget()
{
    using namespace SnapTray::BeautifyBackgroundImage;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("working.png");
    QImage frame(1024, 1024, QImage::Format_RGB32);
    for (int i = 0; i < 15; ++i) {
        frame.fill(QColor(i, 150, 90));
        QVERIFY(frame.save(path));
        const auto preview = loadPreview(path);
        QCOMPARE(preview.thumbnail.pixelColor(0, 0), QColor(i, 150, 90));
        QCOMPARE(loadPreview(path).thumbnail.cacheKey(), preview.thumbnail.cacheKey());
        QVERIFY(cachedBytes() <= 24 * 1024 * 1024);
    }
    qInfo() << "resident thumbnail bytes after 15 images:" << cachedBytes();
}

void tst_BeautifyRenderer::pngThumbnailAndDecodeLimit()
{
    QImage source(2048, 1536, QImage::Format_ARGB32);
    source.fill(Qt::transparent);
    source.setColorSpace(QColorSpace::DisplayP3);
    {
        QPainter painter(&source);
        painter.fillRect(QRect(0, 0, 1024, 768), QColor(200, 100, 25, 128));
    }
    QByteArray png;
    QBuffer buffer(&png);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(source.save(&buffer, "PNG"));
    const auto thumbnail = SnapTray::BeautifyBackgroundImage::decode(png, 128);
    QCOMPARE(thumbnail.size(), QSize(128, 96));
    QCOMPARE(thumbnail.colorSpace(), source.colorSpace());
    QCOMPARE(thumbnail.pixel(0, 0), qPremultiply(source.pixel(0, 0)));
    QCOMPARE(thumbnail.pixelColor(127, 95).alpha(), 0);

    // eXIf orientation 6 in a valid ancillary chunk, following IHDR.
    QByteArray rotated = png;
    rotated.insert(33, QByteArray::fromHex("0000001a6558496649492a0008000000010012010300010000000600000000000000b7481129"));
    QSize orientedSize;
    QCOMPARE(SnapTray::BeautifyBackgroundImage::decode(rotated, 128, &orientedSize),
             thumbnail.transformed(QTransform().rotate(90)));
    QCOMPARE(orientedSize, source.size().transposed());
    QCOMPARE(SnapTray::BeautifyBackgroundImage::decode(rotated),
             SnapTray::BeautifyBackgroundImage::decode(png).transformed(QTransform().rotate(90)));

    // A 16x16 Adam7 RGBA fixture with four solid quadrants.
    const auto interlaced = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAFo9M/3AAAAS0lEQVR4nGP4z/D/PwMIg0gGEMYuAheFycBlcaggrAWHGRha0c3EowBVhAEdE6GAcjcMBl9QOxwIOpqQL4ekAfhVYAQaoVAeegYAANMK/hCJ0SAtAAAAAElFTkSuQmCC");
    const auto reduced = SnapTray::BeautifyBackgroundImage::decode(interlaced, 4);
    QCOMPARE(reduced.size(), QSize(4, 4));
    QCOMPARE(reduced.pixelColor(0, 0), QColor(Qt::magenta));
    QCOMPARE(reduced.pixelColor(3, 0), QColor(Qt::blue));
    QCOMPARE(reduced.pixelColor(0, 3), QColor(Qt::red));
    QCOMPARE(reduced.pixelColor(3, 3), QColor(Qt::black));

    QByteArray bmp;
    QBuffer other(&bmp);
    QVERIFY(other.open(QIODevice::WriteOnly));
    QVERIFY(source.save(&other, "BMP"));
    QVERIFY(SnapTray::BeautifyBackgroundImage::decode(bmp, 128).isNull());

    const int previousLimit = QImageReader::allocationLimit();
    auto restore = qScopeGuard([=] { QImageReader::setAllocationLimit(previousLimit); });
    QImageReader::setAllocationLimit(8);
    QVERIFY(SnapTray::BeautifyBackgroundImage::decode(png, 128).isNull());
    QVERIFY(SnapTray::BeautifyBackgroundImage::decode(png).isNull());
}

QPixmap tst_BeautifyRenderer::createTestPixmap(int w, int h, QColor fill)
{
    QPixmap pixmap(w, h);
    pixmap.fill(fill);
    return pixmap;
}

// ============================================================================
// calculateOutputSize Tests
// ============================================================================

void tst_BeautifyRenderer::testCalculateOutputSize_DefaultPadding()
{
    BeautifySettings settings;
    settings.padding = 64;
    settings.aspectRatio = BeautifyAspectRatio::Auto;

    QSize output = BeautifyRenderer::calculateOutputSize(QSize(200, 100), settings);
    QCOMPARE(output, QSize(200 + 128, 100 + 128));
}

void tst_BeautifyRenderer::testCalculateOutputSize_CustomPadding()
{
    BeautifySettings settings;
    settings.padding = 32;
    settings.aspectRatio = BeautifyAspectRatio::Auto;

    QSize output = BeautifyRenderer::calculateOutputSize(QSize(300, 200), settings);
    QCOMPARE(output, QSize(364, 264));
}

void tst_BeautifyRenderer::testCalculateOutputSize_WithAspectRatio_Auto()
{
    BeautifySettings settings;
    settings.padding = 50;
    settings.aspectRatio = BeautifyAspectRatio::Auto;

    QSize source(400, 300);
    QSize output = BeautifyRenderer::calculateOutputSize(source, settings);
    QCOMPARE(output, QSize(500, 400));
}

void tst_BeautifyRenderer::testCalculateOutputSize_WithAspectRatio_Square()
{
    BeautifySettings settings;
    settings.padding = 50;
    settings.aspectRatio = BeautifyAspectRatio::Square_1_1;

    QSize source(400, 200);
    QSize output = BeautifyRenderer::calculateOutputSize(source, settings);
    // Base: 500x300, square expands to max dimension
    QCOMPARE(output.width(), output.height());
    QVERIFY(output.width() >= 500);
}

void tst_BeautifyRenderer::testCalculateOutputSize_WithAspectRatio_Wide16_9()
{
    BeautifySettings settings;
    settings.padding = 50;
    settings.aspectRatio = BeautifyAspectRatio::Wide_16_9;

    QSize source(400, 200);
    QSize output = BeautifyRenderer::calculateOutputSize(source, settings);
    // Should maintain 16:9 ratio
    // Allow integer rounding tolerance
    double ratio = static_cast<double>(output.width()) / output.height();
    QVERIFY(qAbs(ratio - 16.0 / 9.0) < 0.05);
}

void tst_BeautifyRenderer::testCalculateOutputSize_WithAspectRatio_Tall9_16()
{
    BeautifySettings settings;
    settings.padding = 50;
    settings.aspectRatio = BeautifyAspectRatio::Tall_9_16;

    QSize source(400, 200);
    QSize output = BeautifyRenderer::calculateOutputSize(source, settings);
    QVERIFY(output.height() > output.width());
}

// ============================================================================
// applyToPixmap Tests
// ============================================================================

void tst_BeautifyRenderer::testApplyToPixmap_NullSource()
{
    QPixmap null;
    BeautifySettings settings;
    QPixmap result = BeautifyRenderer::applyToPixmap(null, settings);
    QVERIFY(result.isNull());
}

void tst_BeautifyRenderer::testApplyToPixmap_ProducesNonNullResult()
{
    QPixmap source = createTestPixmap(100, 80);
    BeautifySettings settings;
    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QVERIFY(!result.isNull());
}

void tst_BeautifyRenderer::testApplyToPixmap_OutputSizeMatchesCalculated()
{
    QPixmap source = createTestPixmap(200, 150);
    BeautifySettings settings;
    settings.padding = 40;
    settings.aspectRatio = BeautifyAspectRatio::Auto;

    QSize expected = BeautifyRenderer::calculateOutputSize(source.size(), settings);
    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QCOMPARE(result.size(), expected);
}

void tst_BeautifyRenderer::testApplyToPixmap_SolidBackground()
{
    QPixmap source = createTestPixmap(100, 100);
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::Solid;
    settings.backgroundColor = Qt::blue;
    settings.padding = 50;
    settings.shadowEnabled = false;
    settings.cornerRadius = 0;

    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QVERIFY(!result.isNull());

    // Check a corner pixel has the solid background color
    QImage img = result.toImage();
    QColor corner = img.pixelColor(2, 2);
    QCOMPARE(corner, QColor(Qt::blue));
}

void tst_BeautifyRenderer::testApplyToPixmap_LinearGradient()
{
    QPixmap source = createTestPixmap(100, 100);
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::LinearGradient;
    settings.padding = 50;
    settings.shadowEnabled = false;

    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QVERIFY(!result.isNull());
    QVERIFY(result.width() > source.width());
}

void tst_BeautifyRenderer::testApplyToPixmap_RadialGradient()
{
    QPixmap source = createTestPixmap(100, 100);
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::RadialGradient;
    settings.padding = 50;
    settings.shadowEnabled = false;

    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QVERIFY(!result.isNull());
    QVERIFY(result.width() > source.width());
}

void tst_BeautifyRenderer::testApplyToPixmap_NoShadow()
{
    QPixmap source = createTestPixmap(100, 100);
    BeautifySettings settings;
    settings.shadowEnabled = false;

    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QVERIFY(!result.isNull());
}

void tst_BeautifyRenderer::testApplyToPixmap_WithShadow()
{
    QPixmap source = createTestPixmap(100, 100);
    BeautifySettings settings;
    settings.shadowEnabled = true;
    settings.shadowBlur = 30;

    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QVERIFY(!result.isNull());
}

void tst_BeautifyRenderer::testApplyToPixmap_ZeroCornerRadius()
{
    QPixmap source = createTestPixmap(100, 100);
    BeautifySettings settings;
    settings.cornerRadius = 0;

    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QVERIFY(!result.isNull());
}

void tst_BeautifyRenderer::testApplyToPixmap_HiDPI_CorrectLogicalSize()
{
    // Simulate a Retina display: 200x150 device pixels at DPR=2 = 100x75 logical
    QPixmap source(200, 150);
    source.fill(Qt::red);
    source.setDevicePixelRatio(2.0);

    BeautifySettings settings;
    settings.padding = 40;
    settings.aspectRatio = BeautifyAspectRatio::Auto;

    // Logical source is 100x75, so logical output should be 100+80 x 75+80 = 180x155
    QSize expectedLogical = BeautifyRenderer::calculateOutputSize(QSize(100, 75), settings);
    QCOMPARE(expectedLogical, QSize(180, 155));

    QPixmap result = BeautifyRenderer::applyToPixmap(source, settings);
    QVERIFY(!result.isNull());
    QCOMPARE(result.devicePixelRatio(), 2.0);

    // Device pixel size should be logical * DPR
    QCOMPARE(result.size(), QSize(360, 310));
}

// ============================================================================
// render Tests
// ============================================================================

void tst_BeautifyRenderer::testRender_NullSource_NoOp()
{
    QPixmap canvas(400, 300);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);

    QPixmap nullSource;
    BeautifySettings settings;
    BeautifyRenderer::render(painter, canvas.rect(), nullSource, settings);
    painter.end();

    // Canvas should still be white (render was a no-op)
    QImage img = canvas.toImage();
    QCOMPARE(img.pixelColor(0, 0), QColor(Qt::white));
}

void tst_BeautifyRenderer::testRender_FitsTargetRect()
{
    QPixmap source = createTestPixmap(200, 150);
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::Solid;
    settings.backgroundColor = QColor(0, 100, 200);
    settings.padding = 40;
    settings.shadowEnabled = false;

    QPixmap canvas(400, 300);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    BeautifyRenderer::render(painter, canvas.rect(), source, settings);
    painter.end();

    // The center area should not be white anymore (it has the background)
    QImage img = canvas.toImage();
    QColor center = img.pixelColor(200, 150);
    QVERIFY(center != QColor(Qt::white));
}

void tst_BeautifyRenderer::testApplyToPixmap_BorderInsideThePicture()
{
    // #269: the border is drawn just inside the screenshot; the size stays.
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::Solid;
    settings.backgroundColor = Qt::black;
    settings.shadowEnabled = false;
    settings.cornerRadius = 0;
    settings.padding = 20;
    settings.borderEnabled = true;
    settings.borderWidth = 4;
    settings.borderColor = Qt::green;
    const QPixmap source = createTestPixmap(100, 80, Qt::red);
    const QImage out = BeautifyRenderer::applyToPixmap(source, settings).toImage();
    QCOMPARE(out.size(), QSize(140, 120));
    QCOMPARE(QColor(out.pixel(21, 60)), QColor(Qt::green));  // on the edge, inside
    QCOMPARE(QColor(out.pixel(19, 60)), QColor(Qt::black));  // the background stays
    QCOMPARE(QColor(out.pixel(70, 60)), QColor(Qt::red));    // the picture stays

    settings.borderEnabled = false;
    const QImage plain = BeautifyRenderer::applyToPixmap(source, settings).toImage();
    QCOMPARE(QColor(plain.pixel(21, 60)), QColor(Qt::red));
}

void tst_BeautifyRenderer::testApplyToImage_NullSource()
{
    QVERIFY(BeautifyRenderer::applyToImage(QImage(), BeautifySettings()).isNull());
}

void tst_BeautifyRenderer::testApplyToImage_MatchesApplyToPixmap_data()
{
    QTest::addColumn<int>("background");
    QTest::addColumn<int>("cornerRadius");
    QTest::addColumn<bool>("shadow");
    QTest::addColumn<bool>("border");
    QTest::addColumn<int>("aspect");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<bool>("transparent");

    const int solid = static_cast<int>(BeautifyBackgroundType::Solid);
    const int linear = static_cast<int>(BeautifyBackgroundType::LinearGradient);
    const int radial = static_cast<int>(BeautifyBackgroundType::RadialGradient);
    const int autoRatio = static_cast<int>(BeautifyAspectRatio::Auto);
    const int wide = static_cast<int>(BeautifyAspectRatio::Wide_16_9);
    QTest::newRow("plain") << solid << 0 << false << false << autoRatio << 1.0 << false;
    QTest::newRow("rounded-shadow-border") << linear << 12 << true << true << autoRatio << 1.0 << false;
    QTest::newRow("hidpi-rounded") << radial << 9 << true << false << wide << 2.0 << false;
    QTest::newRow("fractional-dpr") << linear << 6 << false << true << autoRatio << 1.5 << true;
    QTest::newRow("transparent-source") << solid << 10 << true << true << wide << 2.0 << true;
}

void tst_BeautifyRenderer::testApplyToImage_MatchesApplyToPixmap()
{
    QFETCH(int, background);
    QFETCH(int, cornerRadius);
    QFETCH(bool, shadow);
    QFETCH(bool, border);
    QFETCH(int, aspect);
    QFETCH(qreal, dpr);
    QFETCH(bool, transparent);

    QImage source(QSize(150, 96), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            source.setPixelColor(x, y, QColor(x % 256, (y * 3) % 256, (x + y) % 256,
                                              transparent && x < 20 ? 90 : 255));
        }
    }
    source.setDevicePixelRatio(dpr);

    BeautifySettings settings;
    settings.backgroundType = static_cast<BeautifyBackgroundType>(background);
    settings.backgroundColor = QColor(30, 60, 90);
    settings.gradientStartColor = QColor(200, 40, 120);
    settings.gradientEndColor = QColor(20, 180, 220);
    settings.gradientAngle = 35;
    settings.padding = 17;
    settings.cornerRadius = cornerRadius;
    settings.shadowEnabled = shadow;
    settings.shadowBlur = 14;
    settings.shadowOffsetX = 3;
    settings.shadowOffsetY = 5;
    settings.borderEnabled = border;
    settings.borderWidth = 3;
    settings.borderColor = QColor(250, 250, 10);
    settings.aspectRatio = static_cast<BeautifyAspectRatio>(aspect);

    const QImage fromPixmap = BeautifyRenderer::applyToPixmap(
        QPixmap::fromImage(source, Qt::NoOpaqueDetection), settings).toImage();
    const QImage fromImage = BeautifyRenderer::applyToImage(source, settings);

    QCOMPARE(fromImage.size(), fromPixmap.size());
    QCOMPARE(fromImage.devicePixelRatio(), fromPixmap.devicePixelRatio());
    QCOMPARE(fromImage.hasAlphaChannel(), fromPixmap.hasAlphaChannel());
    QCOMPARE(fromImage.convertToFormat(QImage::Format_ARGB32_Premultiplied),
             fromPixmap.convertToFormat(QImage::Format_ARGB32_Premultiplied));
}

QTEST_MAIN(tst_BeautifyRenderer)
#include "tst_BeautifyRenderer.moc"
