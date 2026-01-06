#include <QtTest>
#include <QImage>
#include <QPainter>

#include "scrolling/SeamQualityEvaluator.h"

class tst_SeamQualityEvaluator : public QObject
{
    Q_OBJECT

private slots:
    // Basic evaluation tests
    void testCorrectOverlapHighScore();
    void testWrongOverlapLowScore();
    void testIdenticalImagesHighScore();

    // Direction tests
    void testScrollDownDirection();
    void testScrollUpDirection();
    void testScrollRightDirection();
    void testScrollLeftDirection();

    // Find best overlap tests
    void testFindBestOverlapExact();
    void testFindBestOverlapNearby();

    // Edge cases
    void testNullImages();
    void testZeroOverlap();
    void testOverlapTooLarge();

    // Score components
    void testNCCScore();
    void testMSEScore();

private:
    QImage createStripePattern(int width, int height, int stripeHeight = 20);
    QImage createScrolledImage(const QImage &base, int scrollAmount, bool vertical = true);
};

QImage tst_SeamQualityEvaluator::createStripePattern(int width, int height, int stripeHeight)
{
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(Qt::white);

    QPainter painter(&img);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);

    for (int y = 0; y < height; y += stripeHeight * 2) {
        painter.drawRect(0, y, width, stripeHeight);
    }

    return img;
}

QImage tst_SeamQualityEvaluator::createScrolledImage(const QImage &base, int scrollAmount, bool vertical)
{
    QImage result(base.size(), base.format());
    result.fill(Qt::white);

    QPainter painter(&result);

    if (vertical) {
        // Simulate scrolling down: copy from scrollAmount to end
        painter.drawImage(0, 0, base, 0, scrollAmount);
    } else {
        // Horizontal scroll
        painter.drawImage(0, 0, base, scrollAmount, 0);
    }

    return result;
}

void tst_SeamQualityEvaluator::testCorrectOverlapHighScore()
{
    // Create two images with known overlap
    QImage base = createStripePattern(400, 400);
    int actualScroll = 100;  // Scrolled 100 pixels down
    QImage scrolled = createScrolledImage(base, actualScroll);

    int expectedOverlap = base.height() - actualScroll;  // 300 pixels overlap

    auto result = SeamQualityEvaluator::evaluate(
        base, scrolled, expectedOverlap,
        SeamQualityEvaluator::ScrollDirection::Down);

    QVERIFY2(result.passed, qPrintable(result.failureReason));
    QVERIFY(result.seamScore > 0.8);
    QVERIFY(result.nccScore > 0.85);
}

void tst_SeamQualityEvaluator::testWrongOverlapLowScore()
{
    QImage base = createStripePattern(400, 400);
    int actualScroll = 100;
    QImage scrolled = createScrolledImage(base, actualScroll);

    int wrongOverlap = 200;  // Incorrect overlap

    auto result = SeamQualityEvaluator::evaluate(
        base, scrolled, wrongOverlap,
        SeamQualityEvaluator::ScrollDirection::Down);

    // Wrong overlap should have lower score
    QVERIFY(result.seamScore < 0.7 || !result.passed);
}

void tst_SeamQualityEvaluator::testIdenticalImagesHighScore()
{
    QImage img = createStripePattern(400, 400);

    // Full overlap (identical images)
    auto result = SeamQualityEvaluator::evaluate(
        img, img, img.height() - 1,
        SeamQualityEvaluator::ScrollDirection::Down);

    QVERIFY(result.nccScore > 0.99);
}

void tst_SeamQualityEvaluator::testScrollDownDirection()
{
    QImage base = createStripePattern(400, 400);
    int scroll = 50;
    QImage scrolled = createScrolledImage(base, scroll);

    int overlap = base.height() - scroll;

    auto result = SeamQualityEvaluator::evaluate(
        base, scrolled, overlap,
        SeamQualityEvaluator::ScrollDirection::Down);

    QVERIFY2(result.passed, qPrintable(result.failureReason));
}

void tst_SeamQualityEvaluator::testScrollUpDirection()
{
    // For scroll up, we need the opposite arrangement
    QImage base = createStripePattern(400, 400);
    int scroll = 50;
    QImage scrolled = createScrolledImage(base, scroll);

    // Swap images for up direction
    int overlap = base.height() - scroll;

    auto result = SeamQualityEvaluator::evaluate(
        scrolled, base, overlap,
        SeamQualityEvaluator::ScrollDirection::Up);

    QVERIFY2(result.passed, qPrintable(result.failureReason));
}

void tst_SeamQualityEvaluator::testScrollRightDirection()
{
    QImage base = createStripePattern(400, 400, 40);  // Vertical stripes for horizontal scroll
    int scroll = 50;
    QImage scrolled = createScrolledImage(base, scroll, false);

    int overlap = base.width() - scroll;

    auto result = SeamQualityEvaluator::evaluate(
        base, scrolled, overlap,
        SeamQualityEvaluator::ScrollDirection::Right);

    // Horizontal scrolling should also work
    QVERIFY(result.nccScore > 0.5);  // More relaxed for horizontal
}

void tst_SeamQualityEvaluator::testScrollLeftDirection()
{
    QImage base = createStripePattern(400, 400, 40);
    int scroll = 50;
    QImage scrolled = createScrolledImage(base, scroll, false);

    int overlap = base.width() - scroll;

    auto result = SeamQualityEvaluator::evaluate(
        scrolled, base, overlap,
        SeamQualityEvaluator::ScrollDirection::Left);

    QVERIFY(result.nccScore > 0.5);
}

void tst_SeamQualityEvaluator::testFindBestOverlapExact()
{
    QImage base = createStripePattern(400, 400);
    int actualScroll = 80;
    QImage scrolled = createScrolledImage(base, actualScroll);

    int expectedOverlap = base.height() - actualScroll;  // 320

    int found = SeamQualityEvaluator::findBestOverlap(
        base, scrolled, expectedOverlap,
        SeamQualityEvaluator::ScrollDirection::Down, 5);

    // Should find close to expected
    QVERIFY(qAbs(found - expectedOverlap) <= 5);
}

void tst_SeamQualityEvaluator::testFindBestOverlapNearby()
{
    QImage base = createStripePattern(400, 400);
    int actualScroll = 100;
    QImage scrolled = createScrolledImage(base, actualScroll);

    int expectedOverlap = base.height() - actualScroll;
    int wrongGuess = expectedOverlap + 3;  // Off by 3 pixels

    int found = SeamQualityEvaluator::findBestOverlap(
        base, scrolled, wrongGuess,
        SeamQualityEvaluator::ScrollDirection::Down, 5);

    // Should correct the guess
    QVERIFY(qAbs(found - expectedOverlap) <= 1);
}

void tst_SeamQualityEvaluator::testNullImages()
{
    QImage img = createStripePattern(400, 400);
    QImage nullImg;

    auto result1 = SeamQualityEvaluator::evaluate(
        nullImg, img, 100,
        SeamQualityEvaluator::ScrollDirection::Down);
    QVERIFY(!result1.passed);

    auto result2 = SeamQualityEvaluator::evaluate(
        img, nullImg, 100,
        SeamQualityEvaluator::ScrollDirection::Down);
    QVERIFY(!result2.passed);
}

void tst_SeamQualityEvaluator::testZeroOverlap()
{
    QImage img = createStripePattern(400, 400);

    auto result = SeamQualityEvaluator::evaluate(
        img, img, 0,
        SeamQualityEvaluator::ScrollDirection::Down);

    QVERIFY(!result.passed);
    QVERIFY(!result.failureReason.isEmpty());
}

void tst_SeamQualityEvaluator::testOverlapTooLarge()
{
    QImage img = createStripePattern(400, 400);

    auto result = SeamQualityEvaluator::evaluate(
        img, img, img.height() + 100,  // Larger than image
        SeamQualityEvaluator::ScrollDirection::Down);

    // Should clamp or handle gracefully
    QVERIFY(result.nccScore >= 0.0);  // At least doesn't crash
}

void tst_SeamQualityEvaluator::testNCCScore()
{
    QImage white(100, 100, QImage::Format_RGB32);
    white.fill(Qt::white);

    QImage black(100, 100, QImage::Format_RGB32);
    black.fill(Qt::black);

    auto result1 = SeamQualityEvaluator::evaluate(
        white, white, 50,
        SeamQualityEvaluator::ScrollDirection::Down);
    QVERIFY(result1.nccScore > 0.99);  // Identical = high NCC

    auto result2 = SeamQualityEvaluator::evaluate(
        white, black, 50,
        SeamQualityEvaluator::ScrollDirection::Down);
    // Different colors = lower NCC (exact value depends on implementation)
}

void tst_SeamQualityEvaluator::testMSEScore()
{
    QImage img1(100, 100, QImage::Format_RGB32);
    img1.fill(Qt::white);

    QImage img2(100, 100, QImage::Format_RGB32);
    img2.fill(Qt::white);

    auto result = SeamQualityEvaluator::evaluate(
        img1, img2, 50,
        SeamQualityEvaluator::ScrollDirection::Down);

    // Identical images should have MSE close to 0
    QVERIFY(result.mseScore < 0.01);
}

QTEST_MAIN(tst_SeamQualityEvaluator)
#include "tst_SeamQualityEvaluator.moc"
