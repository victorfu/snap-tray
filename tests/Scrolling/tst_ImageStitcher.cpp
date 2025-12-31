#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include "scrolling/ImageStitcher.h"

class TestImageStitcher : public QObject
{
    Q_OBJECT

private:
    // Helper to create a test image with a solid color
    QImage createSolidImage(int width, int height, const QColor &color)
    {
        QImage img(width, height, QImage::Format_RGB32);
        img.fill(color);
        return img;
    }

    // Helper to create a test image with a gradient for overlap testing
    QImage createGradientImage(int width, int height, bool horizontal = false)
    {
        QImage img(width, height, QImage::Format_RGB32);
        QPainter painter(&img);

        if (horizontal) {
            QLinearGradient gradient(0, 0, width, 0);
            gradient.setColorAt(0.0, Qt::black);
            gradient.setColorAt(1.0, Qt::white);
            painter.fillRect(img.rect(), gradient);
        } else {
            QLinearGradient gradient(0, 0, 0, height);
            gradient.setColorAt(0.0, Qt::black);
            gradient.setColorAt(1.0, Qt::white);
            painter.fillRect(img.rect(), gradient);
        }

        return img;
    }

    // Helper to create an image with a static header (same content across frames)
    QImage createImageWithHeader(int width, int height, int headerHeight,
                                  const QColor &headerColor, const QColor &contentColor,
                                  int contentOffset = 0)
    {
        QImage img(width, height, QImage::Format_RGB32);
        img.fill(contentColor);

        QPainter painter(&img);

        // Draw header
        painter.fillRect(0, 0, width, headerHeight, headerColor);

        // Draw some content pattern that changes between frames
        painter.setPen(Qt::black);
        for (int y = headerHeight + contentOffset; y < height; y += 20) {
            painter.drawLine(0, y, width, y);
        }

        return img;
    }

private slots:
    // =========================================================================
    // Initialization Tests
    // =========================================================================

    void testDefaultConstructor()
    {
        ImageStitcher stitcher;

        QCOMPARE(stitcher.frameCount(), 0);
        QVERIFY(stitcher.getStitchedImage().isNull());
        QCOMPARE(stitcher.algorithm(), ImageStitcher::Algorithm::Auto);
        QCOMPARE(stitcher.captureMode(), ImageStitcher::CaptureMode::Vertical);
    }

    void testStitchConfigDefaults()
    {
        StitchConfig config;

        QCOMPARE(config.templateHeight, 80);
        QCOMPARE(config.searchHeight, 400);
        QVERIFY(qFuzzyCompare(config.confidenceThreshold, 0.85));
        QCOMPARE(config.minOverlap, 20);
        QCOMPARE(config.maxOverlap, 500);
        QVERIFY(qFuzzyCompare(config.staticRowThreshold, 8.0));
        QVERIFY(config.detectStaticRegions);
        QVERIFY(config.useGrayscale);
    }

    void testStaticRegionsDefaults()
    {
        StaticRegions regions;

        QCOMPARE(regions.headerHeight, 0);
        QCOMPARE(regions.footerHeight, 0);
        QCOMPARE(regions.scrollbarWidth, 0);
        QVERIFY(!regions.detected);
    }

    // =========================================================================
    // Configuration Tests
    // =========================================================================

    void testSetAlgorithm()
    {
        ImageStitcher stitcher;

        stitcher.setAlgorithm(ImageStitcher::Algorithm::TemplateMatching);
        QCOMPARE(stitcher.algorithm(), ImageStitcher::Algorithm::TemplateMatching);

        stitcher.setAlgorithm(ImageStitcher::Algorithm::RowProjection);
        QCOMPARE(stitcher.algorithm(), ImageStitcher::Algorithm::RowProjection);

        stitcher.setAlgorithm(ImageStitcher::Algorithm::Auto);
        QCOMPARE(stitcher.algorithm(), ImageStitcher::Algorithm::Auto);
    }

    void testSetCaptureMode()
    {
        ImageStitcher stitcher;

        stitcher.setCaptureMode(ImageStitcher::CaptureMode::Horizontal);
        QCOMPARE(stitcher.captureMode(), ImageStitcher::CaptureMode::Horizontal);

        stitcher.setCaptureMode(ImageStitcher::CaptureMode::Vertical);
        QCOMPARE(stitcher.captureMode(), ImageStitcher::CaptureMode::Vertical);
    }

    void testSetStitchConfig()
    {
        ImageStitcher stitcher;

        StitchConfig config;
        config.templateHeight = 100;
        config.confidenceThreshold = 0.9;
        config.detectStaticRegions = false;

        stitcher.setStitchConfig(config);

        StitchConfig retrieved = stitcher.stitchConfig();
        QCOMPARE(retrieved.templateHeight, 100);
        QVERIFY(qFuzzyCompare(retrieved.confidenceThreshold, 0.9));
        QVERIFY(!retrieved.detectStaticRegions);
    }

    void testConfidenceThresholdClamping()
    {
        ImageStitcher stitcher;

        stitcher.setConfidenceThreshold(1.5);
        QVERIFY(qFuzzyCompare(stitcher.confidenceThreshold(), 1.0));

        stitcher.setConfidenceThreshold(-0.5);
        QVERIFY(qFuzzyCompare(stitcher.confidenceThreshold(), 0.0));

        stitcher.setConfidenceThreshold(0.75);
        QVERIFY(qFuzzyCompare(stitcher.confidenceThreshold(), 0.75));
    }

    void testLegacyDetectFixedElements()
    {
        ImageStitcher stitcher;

        // Initially enabled
        QVERIFY(stitcher.detectFixedElements());

        stitcher.setDetectFixedElements(false);
        QVERIFY(!stitcher.detectFixedElements());

        // Verify it updates the config
        QVERIFY(!stitcher.stitchConfig().detectStaticRegions);
    }

    // =========================================================================
    // First Frame Tests
    // =========================================================================

    void testAddFirstFrame()
    {
        ImageStitcher stitcher;
        QImage frame = createSolidImage(400, 300, Qt::red);

        auto result = stitcher.addFrame(frame);

        QVERIFY(result.success);
        QVERIFY(qFuzzyCompare(result.confidence, 1.0));
        QCOMPARE(stitcher.frameCount(), 1);
        QCOMPARE(stitcher.getCurrentSize(), QSize(400, 300));
    }

    void testAddEmptyFrame()
    {
        ImageStitcher stitcher;
        QImage emptyFrame;

        auto result = stitcher.addFrame(emptyFrame);

        QVERIFY(!result.success);
        QCOMPARE(result.failureReason, QString("Empty frame"));
        QCOMPARE(stitcher.frameCount(), 0);
    }

    void testReset()
    {
        ImageStitcher stitcher;
        QImage frame = createSolidImage(400, 300, Qt::red);

        stitcher.addFrame(frame);
        QCOMPARE(stitcher.frameCount(), 1);

        stitcher.reset();

        QCOMPARE(stitcher.frameCount(), 0);
        QVERIFY(stitcher.getStitchedImage().isNull());
    }

    // =========================================================================
    // Frame Change Detection Tests
    // =========================================================================

    void testIdenticalFramesNotChanged()
    {
        QImage frame1 = createSolidImage(100, 100, Qt::red);
        QImage frame2 = createSolidImage(100, 100, Qt::red);

        QVERIFY(!ImageStitcher::isFrameChanged(frame1, frame2));
    }

    void testDifferentFramesChanged()
    {
        QImage frame1 = createSolidImage(100, 100, Qt::red);
        QImage frame2 = createSolidImage(100, 100, Qt::blue);

        QVERIFY(ImageStitcher::isFrameChanged(frame1, frame2));
    }

    void testDifferentSizedFramesChanged()
    {
        QImage frame1 = createSolidImage(100, 100, Qt::red);
        QImage frame2 = createSolidImage(200, 200, Qt::red);

        QVERIFY(ImageStitcher::isFrameChanged(frame1, frame2));
    }

    void testUnchangedFrameSkipped()
    {
        ImageStitcher stitcher;
        QImage frame = createSolidImage(400, 300, Qt::red);

        stitcher.addFrame(frame);
        auto result = stitcher.addFrame(frame);  // Same frame

        QVERIFY(!result.success);
        QCOMPARE(result.failureReason, QString("Frame unchanged"));
        QCOMPARE(stitcher.frameCount(), 1);  // Still 1, not incremented
    }

    // =========================================================================
    // StitchResult Tests
    // =========================================================================

    void testStitchResultDefaults()
    {
        ImageStitcher::StitchResult result;

        QVERIFY(!result.success);
        QVERIFY(qFuzzyCompare(result.confidence, 0.0));
        QCOMPARE(result.overlapPixels, 0);
        QCOMPARE(result.usedAlgorithm, ImageStitcher::Algorithm::TemplateMatching);
        QCOMPARE(result.direction, ImageStitcher::ScrollDirection::Down);
        QVERIFY(result.failureReason.isEmpty());
    }

    // =========================================================================
    // Static Region Detection Tests
    // =========================================================================

    void testNoStaticRegionsInitially()
    {
        ImageStitcher stitcher;

        StaticRegions regions = stitcher.detectedStaticRegions();

        QVERIFY(!regions.detected);
        QCOMPARE(regions.headerHeight, 0);
        QCOMPARE(regions.footerHeight, 0);
        QCOMPARE(regions.scrollbarWidth, 0);
    }

    // =========================================================================
    // Algorithm Enum Tests (RSSA: No ORB/SIFT)
    // =========================================================================

    void testAlgorithmEnumValues()
    {
        // Verify the RSSA refactor removed ORB and SIFT
        // The enum should only have: TemplateMatching, RowProjection, Auto

        ImageStitcher stitcher;

        // These should compile and work
        stitcher.setAlgorithm(ImageStitcher::Algorithm::TemplateMatching);
        QCOMPARE(stitcher.algorithm(), ImageStitcher::Algorithm::TemplateMatching);

        stitcher.setAlgorithm(ImageStitcher::Algorithm::RowProjection);
        QCOMPARE(stitcher.algorithm(), ImageStitcher::Algorithm::RowProjection);

        stitcher.setAlgorithm(ImageStitcher::Algorithm::Auto);
        QCOMPARE(stitcher.algorithm(), ImageStitcher::Algorithm::Auto);
    }

    // =========================================================================
    // Capture Mode Tests
    // =========================================================================

    void testVerticalCaptureMode()
    {
        ImageStitcher stitcher;
        stitcher.setCaptureMode(ImageStitcher::CaptureMode::Vertical);

        QImage frame = createGradientImage(400, 300, false);
        stitcher.addFrame(frame);

        QSize size = stitcher.getCurrentSize();
        QCOMPARE(size.width(), 400);
        QCOMPARE(size.height(), 300);
    }

    void testHorizontalCaptureMode()
    {
        ImageStitcher stitcher;
        stitcher.setCaptureMode(ImageStitcher::CaptureMode::Horizontal);

        QImage frame = createGradientImage(400, 300, true);
        stitcher.addFrame(frame);

        QSize size = stitcher.getCurrentSize();
        QCOMPARE(size.width(), 400);
        QCOMPARE(size.height(), 300);
    }

    // =========================================================================
    // Viewport Rect Tests
    // =========================================================================

    void testCurrentViewportRect()
    {
        ImageStitcher stitcher;
        QImage frame = createSolidImage(400, 300, Qt::red);

        stitcher.addFrame(frame);

        QRect viewport = stitcher.currentViewportRect();
        QCOMPARE(viewport, QRect(0, 0, 400, 300));
    }

    void testViewportRectEmptyBeforeFirstFrame()
    {
        ImageStitcher stitcher;

        QRect viewport = stitcher.currentViewportRect();
        QVERIFY(viewport.isNull() || !viewport.isValid());
    }
};

QTEST_MAIN(TestImageStitcher)
#include "tst_ImageStitcher.moc"
