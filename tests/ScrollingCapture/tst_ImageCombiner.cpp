#include <QtTest>
#include "scrolling/ImageCombiner.h"
#include <QImage>
#include <QPainter>

/**
 * @brief Test class for ImageCombiner
 *
 * Tests the image stitching algorithm including:
 * - Basic overlap detection
 * - Sticky footer detection
 * - Side margin handling
 * - Fallback matching
 * - Edge cases
 */
class tst_ImageCombiner : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Basic functionality
    void testInitialState();
    void testReset();
    void testSetFirstFrame();

    // Overlap detection
    void testAddFrame_PerfectOverlap();
    void testAddFrame_PartialOverlap();
    void testAddFrame_NoOverlap();
    void testAddFrame_MinMatchThreshold();

    // Sticky footer detection
    void testStickyFooter_Detected();
    void testStickyFooter_Disabled();
    void testStickyFooter_NoFooter();

    // Side margin handling
    void testSideMargin_IgnoresEdges();
    void testSideMargin_NarrowImage();
    void testSideMargin_WideImage();

    // Fallback matching
    void testFallback_UsesLastSuccessful();
    void testFallback_NoFallbackAvailable();

    // Edge cases
    void testAddFrame_EmptyImage();
    void testAddFrame_DifferentSizes();
    void testAddFrame_DifferentWidths();
    void testAddFrame_FormatConversion();
    void testMultipleFrames();

private:
    ImageCombiner* m_combiner;

    // Helper methods
    QImage createTestImage(int width, int height, QColor color);
    QImage createImageWithContent(int width, int height, int contentOffset,
                                  QImage::Format format = QImage::Format_ARGB32);
    QImage createImageWithFooter(int width, int height, int footerHeight, QColor footerColor);
};

void tst_ImageCombiner::init()
{
    m_combiner = new ImageCombiner();
}

void tst_ImageCombiner::cleanup()
{
    delete m_combiner;
    m_combiner = nullptr;
}

// Helper: Create a solid color test image
QImage tst_ImageCombiner::createTestImage(int width, int height, QColor color)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

// Helper: Create an image with distinguishable content at different offsets
QImage tst_ImageCombiner::createImageWithContent(int width, int height, int contentOffset,
                                                 QImage::Format format)
{
    QImage image(width, height, format);
    image.fill(Qt::white);

    QPainter painter(&image);
    // Draw horizontal lines at specific positions to make matching work
    for (int y = 0; y < height; y++) {
        // Create unique content based on (y + contentOffset)
        int value = (y + contentOffset) % 256;
        painter.setPen(QColor(value, value, value));
        painter.drawLine(0, y, width - 1, y);
    }
    return image;
}

// Helper: Create an image with a fixed footer
QImage tst_ImageCombiner::createImageWithFooter(int width, int height, int footerHeight, QColor footerColor)
{
    QImage image = createImageWithContent(width, height - footerHeight, 0);
    QImage resized(width, height, QImage::Format_ARGB32);
    resized.fill(Qt::white);

    QPainter painter(&resized);
    painter.drawImage(0, 0, image);
    painter.fillRect(0, height - footerHeight, width, footerHeight, footerColor);
    return resized;
}

void tst_ImageCombiner::testInitialState()
{
    QCOMPARE(m_combiner->frameCount(), 0);
    QCOMPARE(m_combiner->totalHeight(), 0);
    QVERIFY(m_combiner->resultImage().isNull());
}

void tst_ImageCombiner::testReset()
{
    QImage frame = createTestImage(100, 100, Qt::red);
    m_combiner->setFirstFrame(frame);
    QCOMPARE(m_combiner->frameCount(), 1);

    m_combiner->reset();
    QCOMPARE(m_combiner->frameCount(), 0);
    QVERIFY(m_combiner->resultImage().isNull());
}

void tst_ImageCombiner::testSetFirstFrame()
{
    QImage frame = createTestImage(200, 300, Qt::blue);
    m_combiner->setFirstFrame(frame);

    QCOMPARE(m_combiner->frameCount(), 1);
    QCOMPARE(m_combiner->totalHeight(), 300);
    QCOMPARE(m_combiner->resultImage().size(), frame.size());
}

void tst_ImageCombiner::testAddFrame_PerfectOverlap()
{
    // Create two frames with 50% overlap
    const int width = 200;
    const int height = 100;
    const int overlap = 50;

    QImage frame1 = createImageWithContent(width, height, 0);
    QImage frame2 = createImageWithContent(width, height, height - overlap);

    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    QCOMPARE(result.status, CaptureStatus::Successful);
    QVERIFY(result.matchCount >= 10);  // Should find substantial match
    QCOMPARE(m_combiner->frameCount(), 2);
}

void tst_ImageCombiner::testAddFrame_PartialOverlap()
{
    const int width = 200;
    const int height = 100;

    QImage frame1 = createImageWithContent(width, height, 0);
    QImage frame2 = createImageWithContent(width, height, 70);  // 30px overlap

    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    // Should still find the overlap
    QCOMPARE(result.status, CaptureStatus::Successful);
    QCOMPARE(m_combiner->frameCount(), 2);
}

void tst_ImageCombiner::testAddFrame_NoOverlap()
{
    const int width = 200;
    const int height = 100;

    QImage frame1 = createTestImage(width, height, Qt::red);
    QImage frame2 = createTestImage(width, height, Qt::blue);

    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    // No overlap found, but fallback estimation is used
    QCOMPARE(result.status, CaptureStatus::PartiallySuccessful);
}

void tst_ImageCombiner::testAddFrame_MinMatchThreshold()
{
    m_combiner->setOptions(20, false);  // Require 20 matching lines

    const int width = 200;
    const int height = 100;

    QImage frame1 = createImageWithContent(width, height, 0);
    QImage frame2 = createImageWithContent(width, height, 85);  // Only 15px overlap

    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    // Insufficient match, but fallback estimation is used
    QCOMPARE(result.status, CaptureStatus::PartiallySuccessful);
}

void tst_ImageCombiner::testStickyFooter_Detected()
{
    const int width = 200;
    const int height = 100;
    const int footerHeight = 20;

    // Create two overlapping frames with same sticky footer
    // Frame 1: content with footer
    QImage frame1(width, height, QImage::Format_ARGB32);
    frame1.fill(Qt::white);
    QPainter p1(&frame1);
    // Content region (lines 0-79): unique gradient
    for (int y = 0; y < height - footerHeight; y++) {
        p1.setPen(QColor(y, y, y));
        p1.drawLine(0, y, width - 1, y);
    }
    // Footer region (lines 80-99): solid gray (sticky)
    p1.fillRect(0, height - footerHeight, width, footerHeight, Qt::gray);
    p1.end();

    // Frame 2: scrolled by 30px (so top 30px of content match bottom 30px of frame1's content)
    // plus the same sticky footer
    QImage frame2(width, height, QImage::Format_ARGB32);
    frame2.fill(Qt::white);
    QPainter p2(&frame2);
    // Content region: starts where frame1 content left off at y=50
    int scrollOffset = 50;
    for (int y = 0; y < height - footerHeight; y++) {
        int value = (scrollOffset + y) % 256;
        p2.setPen(QColor(value, value, value));
        p2.drawLine(0, y, width - 1, y);
    }
    // Same sticky footer
    p2.fillRect(0, height - footerHeight, width, footerHeight, Qt::gray);
    p2.end();

    m_combiner->setOptions(10, true);  // Enable auto-ignore bottom
    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    // Should find overlap and detect footer
    QCOMPARE(result.status, CaptureStatus::Successful);
    QVERIFY(result.ignoreBottom > 0);
}

void tst_ImageCombiner::testStickyFooter_Disabled()
{
    const int width = 200;
    const int height = 100;
    const int footerHeight = 20;

    QImage frame1 = createImageWithFooter(width, height, footerHeight, Qt::gray);
    QImage frame2 = createImageWithFooter(width, height, footerHeight, Qt::gray);

    m_combiner->setOptions(10, false);  // Disable auto-ignore bottom
    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    // Should not detect footer
    QCOMPARE(result.ignoreBottom, 0);
}

void tst_ImageCombiner::testStickyFooter_NoFooter()
{
    const int width = 200;
    const int height = 100;

    QImage frame1 = createImageWithContent(width, height, 0);
    QImage frame2 = createImageWithContent(width, height, 50);

    m_combiner->setOptions(10, true);
    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    // No sticky footer
    QCOMPARE(result.ignoreBottom, 0);
}

void tst_ImageCombiner::testSideMargin_IgnoresEdges()
{
    // Verify side margin calculation for normal-width images
    int margin100 = ScrollingCaptureOptions::computeSideMargin(2000);
    QCOMPARE(margin100, 100);  // 2000/20 = 100

    int margin200 = ScrollingCaptureOptions::computeSideMargin(4000);
    QCOMPARE(margin200, 200);  // 4000/20 = 200
}

void tst_ImageCombiner::testSideMargin_NarrowImage()
{
    // For narrow images (width < 150), use smaller margin to preserve comparison area
    int margin60 = ScrollingCaptureOptions::computeSideMargin(60);
    QCOMPARE(margin60, 10);  // qMax(10, 60/10) = 10

    int margin100 = ScrollingCaptureOptions::computeSideMargin(100);
    QCOMPARE(margin100, 10);  // qMax(10, 100/10) = 10

    int margin140 = ScrollingCaptureOptions::computeSideMargin(140);
    QCOMPARE(margin140, 14);  // qMax(10, 140/10) = 14
}

void tst_ImageCombiner::testSideMargin_WideImage()
{
    // For wide images, cap at width/3
    int margin6000 = ScrollingCaptureOptions::computeSideMargin(6000);
    QCOMPARE(margin6000, 300);  // min(6000/20, 6000/3) = min(300, 2000) = 300

    // Boundary test at width/3 cap
    int margin3000 = ScrollingCaptureOptions::computeSideMargin(3000);
    QCOMPARE(margin3000, 150);  // 3000/20 = 150, which is < 3000/3 = 1000
}

void tst_ImageCombiner::testFallback_UsesLastSuccessful()
{
    const int width = 200;
    const int height = 100;

    // First successful combine
    QImage frame1 = createImageWithContent(width, height, 0);
    QImage frame2 = createImageWithContent(width, height, 50);

    m_combiner->setFirstFrame(frame1);
    CombineResult result1 = m_combiner->addFrame(frame2);
    QCOMPARE(result1.status, CaptureStatus::Successful);

    // Third frame with no detectable overlap but should use fallback
    QImage frame3 = createTestImage(width, height, Qt::green);
    CombineResult result2 = m_combiner->addFrame(frame3);

    QCOMPARE(result2.status, CaptureStatus::PartiallySuccessful);
}

void tst_ImageCombiner::testFallback_NoFallbackAvailable()
{
    const int width = 200;
    const int height = 100;

    QImage frame1 = createTestImage(width, height, Qt::red);
    QImage frame2 = createTestImage(width, height, Qt::blue);

    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    // No match found, but last-resort fallback estimation is used
    QCOMPARE(result.status, CaptureStatus::PartiallySuccessful);
}

void tst_ImageCombiner::testAddFrame_EmptyImage()
{
    QImage frame1 = createTestImage(100, 100, Qt::red);
    m_combiner->setFirstFrame(frame1);

    QImage emptyFrame;
    CombineResult result = m_combiner->addFrame(emptyFrame);

    // Should handle gracefully
    QCOMPARE(result.status, CaptureStatus::Failed);
}

void tst_ImageCombiner::testAddFrame_DifferentSizes()
{
    QImage frame1 = createTestImage(200, 100, Qt::red);
    QImage frame2 = createTestImage(200, 150, Qt::blue);  // Different height

    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    // Should attempt to match despite different heights
    // (will likely fail unless there's actual overlap)
    QVERIFY(result.status == CaptureStatus::Failed ||
            result.status == CaptureStatus::PartiallySuccessful);
}

void tst_ImageCombiner::testAddFrame_DifferentWidths()
{
    QImage frame1 = createTestImage(200, 100, Qt::red);
    QImage frame2 = createTestImage(201, 100, Qt::blue);  // Different width

    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    QCOMPARE(result.status, CaptureStatus::Failed);
}

void tst_ImageCombiner::testAddFrame_FormatConversion()
{
    const int width = 200;
    const int height = 100;

    QImage frame1 = createImageWithContent(width, height, 0, QImage::Format_RGB32);
    QImage frame2 = createImageWithContent(width, height, 50, QImage::Format_RGB32);

    m_combiner->setFirstFrame(frame1);
    CombineResult result = m_combiner->addFrame(frame2);

    QCOMPARE(result.status, CaptureStatus::Successful);
}

void tst_ImageCombiner::testMultipleFrames()
{
    const int width = 200;
    const int height = 100;

    // Simulate scrolling with 50% overlap between frames
    m_combiner->setFirstFrame(createImageWithContent(width, height, 0));

    int totalExpectedHeight = height;
    for (int i = 1; i < 5; i++) {
        QImage frame = createImageWithContent(width, height, i * 50);
        CombineResult result = m_combiner->addFrame(frame);

        if (result.status == CaptureStatus::Successful) {
            totalExpectedHeight += height - result.matchCount;
        }
    }

    QCOMPARE(m_combiner->frameCount(), 5);
    QVERIFY(m_combiner->totalHeight() > height);  // Should be taller than single frame
}

QTEST_MAIN(tst_ImageCombiner)
#include "tst_ImageCombiner.moc"
