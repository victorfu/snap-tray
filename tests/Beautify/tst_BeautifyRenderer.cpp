#include <QtTest/QtTest>
#include <QPainter>
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

    // render tests
    void testRender_NullSource_NoOp();
    void testRender_FitsTargetRect();

private:
    QPixmap createTestPixmap(int w, int h, QColor fill = Qt::red);
};

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

QTEST_MAIN(tst_BeautifyRenderer)
#include "tst_BeautifyRenderer.moc"
