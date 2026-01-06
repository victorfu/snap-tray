#include <QtTest>
#include <QImage>
#include <QPainter>

#include "scrolling/FrameStabilityDetector.h"

class tst_FrameStabilityDetector : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Initialization tests
    void testDefaultConfig();
    void testSetConfig();

    // Stability detection tests
    void testIdenticalFramesAreStable();
    void testDifferentFramesAreUnstable();
    void testConsecutiveStableRequired();
    void testReset();

    // ROI extraction tests
    void testROIWithFixedHeader();
    void testROIWithFixedFooter();

    // Static utility tests
    void testCompareFramesStatic();

    // Edge cases
    void testNullFrame();
    void testTimeout();

private:
    QImage createTestFrame(int width, int height, QColor color);
    QImage createPatternFrame(int width, int height, int patternOffset = 0);

    FrameStabilityDetector *m_detector = nullptr;
};

void tst_FrameStabilityDetector::init()
{
    m_detector = new FrameStabilityDetector();
}

void tst_FrameStabilityDetector::cleanup()
{
    delete m_detector;
    m_detector = nullptr;
}

QImage tst_FrameStabilityDetector::createTestFrame(int width, int height, QColor color)
{
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(color);
    return img;
}

QImage tst_FrameStabilityDetector::createPatternFrame(int width, int height, int patternOffset)
{
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(Qt::white);

    QPainter painter(&img);
    painter.setPen(Qt::black);

    // Draw horizontal stripes
    for (int y = patternOffset; y < height; y += 20) {
        painter.drawLine(0, y, width, y);
    }

    return img;
}

void tst_FrameStabilityDetector::testDefaultConfig()
{
    auto config = m_detector->config();
    QCOMPARE(config.roiMarginPercent, 15);
    QVERIFY(config.stabilityThreshold > 0.9);
    QVERIFY(config.consecutiveStableRequired >= 2);
}

void tst_FrameStabilityDetector::testSetConfig()
{
    FrameStabilityDetector::Config config;
    config.stabilityThreshold = 0.95;
    config.consecutiveStableRequired = 3;
    config.roiMarginPercent = 20;

    m_detector->setConfig(config);
    auto retrieved = m_detector->config();

    QCOMPARE(retrieved.stabilityThreshold, 0.95);
    QCOMPARE(retrieved.consecutiveStableRequired, 3);
    QCOMPARE(retrieved.roiMarginPercent, 20);
}

void tst_FrameStabilityDetector::testIdenticalFramesAreStable()
{
    QImage frame = createTestFrame(400, 400, Qt::blue);

    // First frame - no comparison possible
    auto result1 = m_detector->addFrame(frame);
    QVERIFY(!result1.stable);

    // Second frame (identical) - one stable frame
    auto result2 = m_detector->addFrame(frame);
    QCOMPARE(result2.consecutiveStableCount, 1);

    // Third frame (identical) - should be stable now (2 consecutive)
    auto result3 = m_detector->addFrame(frame);
    QVERIFY(result3.stable);
    QVERIFY(result3.stabilityScore > 0.98);
}

void tst_FrameStabilityDetector::testDifferentFramesAreUnstable()
{
    // Create frames with different patterns
    QImage frame1 = createPatternFrame(400, 400, 0);
    QImage frame2 = createPatternFrame(400, 400, 10);  // Shifted pattern

    m_detector->addFrame(frame1);
    auto result = m_detector->addFrame(frame2);

    QVERIFY(!result.stable);
    QCOMPARE(result.consecutiveStableCount, 0);
}

void tst_FrameStabilityDetector::testConsecutiveStableRequired()
{
    FrameStabilityDetector::Config config;
    config.consecutiveStableRequired = 3;
    m_detector->setConfig(config);

    QImage frame = createTestFrame(400, 400, Qt::green);

    m_detector->addFrame(frame);

    // Second frame - 1 consecutive
    auto result2 = m_detector->addFrame(frame);
    QCOMPARE(result2.consecutiveStableCount, 1);
    QVERIFY(!result2.stable);

    // Third frame - 2 consecutive
    auto result3 = m_detector->addFrame(frame);
    QCOMPARE(result3.consecutiveStableCount, 2);
    QVERIFY(!result3.stable);

    // Fourth frame - 3 consecutive, should be stable
    auto result4 = m_detector->addFrame(frame);
    QCOMPARE(result4.consecutiveStableCount, 3);
    QVERIFY(result4.stable);
}

void tst_FrameStabilityDetector::testReset()
{
    QImage frame = createTestFrame(400, 400, Qt::red);

    // Build up some state
    m_detector->addFrame(frame);
    m_detector->addFrame(frame);
    auto result1 = m_detector->addFrame(frame);
    QVERIFY(result1.stable);

    // Reset
    m_detector->reset();

    // State should be cleared
    auto resultAfterReset = m_detector->currentResult();
    QVERIFY(!resultAfterReset.stable);
    QCOMPARE(resultAfterReset.totalFramesChecked, 0);
    QCOMPARE(resultAfterReset.consecutiveStableCount, 0);
}

void tst_FrameStabilityDetector::testROIWithFixedHeader()
{
    FrameStabilityDetector::Config config;
    config.fixedHeaderRect = QRect(0, 0, 400, 50);  // 50px header
    m_detector->setConfig(config);

    // Create frames where header is fixed but content scrolls
    QImage frame1 = createPatternFrame(400, 400, 0);
    QImage frame2 = createPatternFrame(400, 400, 5);

    m_detector->addFrame(frame1);
    auto result = m_detector->addFrame(frame2);

    // Should detect the change in the non-header area
    QVERIFY(!result.stable);
}

void tst_FrameStabilityDetector::testROIWithFixedFooter()
{
    FrameStabilityDetector::Config config;
    config.fixedFooterRect = QRect(0, 350, 400, 50);  // 50px footer
    m_detector->setConfig(config);

    QImage frame1 = createPatternFrame(400, 400, 0);
    QImage frame2 = createPatternFrame(400, 400, 5);

    m_detector->addFrame(frame1);
    auto result = m_detector->addFrame(frame2);

    QVERIFY(!result.stable);
}

void tst_FrameStabilityDetector::testCompareFramesStatic()
{
    QImage frame1 = createTestFrame(400, 400, Qt::blue);
    QImage frame2 = createTestFrame(400, 400, Qt::blue);
    QImage frame3 = createTestFrame(400, 400, Qt::red);

    // Identical frames should have high similarity
    double sim1 = FrameStabilityDetector::compareFrames(frame1, frame2);
    QVERIFY(sim1 > 0.99);

    // Different frames should have low similarity
    double sim2 = FrameStabilityDetector::compareFrames(frame1, frame3);
    QVERIFY(sim2 < 0.5);
}

void tst_FrameStabilityDetector::testNullFrame()
{
    QImage nullFrame;
    QImage validFrame = createTestFrame(400, 400, Qt::blue);

    m_detector->addFrame(validFrame);
    auto result = m_detector->addFrame(nullFrame);

    // Should handle null frame gracefully
    QVERIFY(!result.stable);
}

void tst_FrameStabilityDetector::testTimeout()
{
    FrameStabilityDetector::Config config;
    config.maxWaitFrames = 3;
    config.consecutiveStableRequired = 10;  // Impossible to reach
    m_detector->setConfig(config);

    // Create always-changing frames
    for (int i = 0; i < 5; ++i) {
        QImage frame = createPatternFrame(400, 400, i * 5);
        auto result = m_detector->addFrame(frame);

        if (result.timedOut) {
            QVERIFY(result.totalFramesChecked > config.maxWaitFrames);
            return;
        }
    }

    // Should have timed out
    QVERIFY(m_detector->currentResult().timedOut);
}

QTEST_MAIN(tst_FrameStabilityDetector)
#include "tst_FrameStabilityDetector.moc"
