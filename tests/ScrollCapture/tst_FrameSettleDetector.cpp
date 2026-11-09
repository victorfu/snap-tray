#include "scrollcapture/FrameSettleDetector.h"

#include <QImage>
#include <QtTest>

using namespace snaptray::scrollcapture;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 320;

QImage frameWithShade(int shade)
{
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    for (int y = 0; y < kHeight; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < kWidth; ++x) {
            line[x] = qRgb((x + shade) & 0xFF, (y + shade) & 0xFF, shade & 0xFF);
        }
    }
    return image;
}

QImage withCaret(const QImage& frame, bool visible)
{
    QImage copy = frame.copy();
    if (visible) {
        for (int y = 10; y < 20; ++y) {
            copy.setPixel(5, y, qRgb(0, 0, 0));
        }
    }
    return copy;
}

using Outcome = FrameSettleDetector::Outcome;

} // namespace

class tst_FrameSettleDetector : public QObject
{
    Q_OBJECT

private slots:
    void settlesAfterMotionStops();
    void strictPairsRequireLongerRuns();
    void appThatHasNotReactedIsNotSettledEarly();
    void noMotionSettlesAfterGrace();
    void blinkingCaretTimesOutButRelaxedSettles();
    void continuousMotionTimesOutWithoutRelaxedSettle();
    void ignoresFramesAfterFinalResult();
    void repeatedCaptureOfOneFrameIsNotSettled();
    void caretBeforeScrollKeepsGracePeriod();
    void caretOnlyIsNotMotion();
    void countsChangedTilesIncludingPartialEdges();
};

void tst_FrameSettleDetector::settlesAfterMotionStops()
{
    FrameSettleDetector detector;
    detector.start(frameWithShade(0));
    QCOMPARE(detector.addFrame(frameWithShade(10), 16).outcome, Outcome::Pending);
    QCOMPARE(detector.addFrame(frameWithShade(20), 33).outcome, Outcome::Pending);
    QCOMPARE(detector.addFrame(frameWithShade(20), 60).outcome, Outcome::Pending);
    const auto result = detector.addFrame(frameWithShade(20), 90);
    QCOMPARE(result.outcome, Outcome::Settled);
    QCOMPARE(result.strictSettleMs.value(), 33);
    QCOMPARE(result.strictConfirmedMs.value(), 90);
    QCOMPARE(result.relaxedSettleMs.value(), 33);
    QVERIFY(result.motionObserved);
    QCOMPARE(result.frames, 4);
}

void tst_FrameSettleDetector::strictPairsRequireLongerRuns()
{
    FrameSettleDetector::Config config;
    config.strictPairs = 2;
    config.minStableMs = 0;
    FrameSettleDetector detector(config);
    detector.start(frameWithShade(0));
    detector.addFrame(frameWithShade(10), 10);
    QCOMPARE(detector.addFrame(frameWithShade(10), 20).outcome, Outcome::Pending);
    const auto result = detector.addFrame(frameWithShade(10), 30);
    QCOMPARE(result.outcome, Outcome::Settled);
    QCOMPARE(result.strictSettleMs.value(), 10);
}

void tst_FrameSettleDetector::appThatHasNotReactedIsNotSettledEarly()
{
    // The first frames still show the old content; they must not end
    // detection before the grace period.
    FrameSettleDetector detector;
    const QImage start = frameWithShade(0);
    detector.start(start);
    QCOMPARE(detector.addFrame(start, 20).outcome, Outcome::Pending);
    QCOMPARE(detector.addFrame(start, 40).outcome, Outcome::Pending);
    QCOMPARE(detector.addFrame(frameWithShade(30), 60).outcome, Outcome::Pending);
    const auto result = detector.addFrame(frameWithShade(30), 120);
    QCOMPARE(result.outcome, Outcome::Settled);
    QCOMPARE(result.strictSettleMs.value(), 60);
    QVERIFY(result.motionObserved);
}

void tst_FrameSettleDetector::noMotionSettlesAfterGrace()
{
    FrameSettleDetector::Config config;
    config.noMotionGraceMs = 100;
    FrameSettleDetector detector(config);
    const QImage start = frameWithShade(0);
    detector.start(start);
    QCOMPARE(detector.addFrame(start, 50).outcome, Outcome::Pending);
    const auto result = detector.addFrame(start, 120);
    QCOMPARE(result.outcome, Outcome::Settled);
    QVERIFY(!result.motionObserved);
}

void tst_FrameSettleDetector::blinkingCaretTimesOutButRelaxedSettles()
{
    FrameSettleDetector::Config config;
    config.timeoutMs = 200;
    FrameSettleDetector detector(config);
    const QImage scrolled = frameWithShade(40);
    detector.start(frameWithShade(0));
    detector.addFrame(withCaret(scrolled, true), 20);
    FrameSettleDetector::Result result;
    bool caret = false;
    for (qint64 t = 40; result.outcome == Outcome::Pending; t += 20) {
        result = detector.addFrame(withCaret(scrolled, caret), t);
        caret = !caret;
    }
    QCOMPARE(result.outcome, Outcome::TimedOut);
    QVERIFY(!result.strictSettleMs.has_value());
    QCOMPARE(result.relaxedSettleMs.value(), 20);
    QCOMPARE(result.lastChangedTiles, 1);
}

void tst_FrameSettleDetector::continuousMotionTimesOutWithoutRelaxedSettle()
{
    FrameSettleDetector::Config config;
    config.timeoutMs = 100;
    FrameSettleDetector detector(config);
    detector.start(frameWithShade(0));
    FrameSettleDetector::Result result;
    for (int step = 1; result.outcome == Outcome::Pending; ++step) {
        result = detector.addFrame(frameWithShade(step * 7), step * 20);
    }
    QCOMPARE(result.outcome, Outcome::TimedOut);
    QVERIFY(!result.relaxedSettleMs.has_value());
}

void tst_FrameSettleDetector::ignoresFramesAfterFinalResult()
{
    FrameSettleDetector detector;
    detector.start(frameWithShade(0));
    detector.addFrame(frameWithShade(5), 10);
    detector.addFrame(frameWithShade(5), 70);
    const auto after = detector.addFrame(frameWithShade(99), 80);
    QCOMPARE(after.outcome, Outcome::Settled);
    QCOMPARE(after.frames, 2);
}

void tst_FrameSettleDetector::repeatedCaptureOfOneFrameIsNotSettled()
{
    // Two captures 5 ms apart can both show one intermediate animation frame;
    // that must not end detection before the next frame appears.
    FrameSettleDetector detector;
    detector.start(frameWithShade(0));
    QCOMPARE(detector.addFrame(frameWithShade(10), 10).outcome, Outcome::Pending);
    QCOMPARE(detector.addFrame(frameWithShade(10), 15).outcome, Outcome::Pending);
    QCOMPARE(detector.addFrame(frameWithShade(20), 27).outcome, Outcome::Pending);
    const auto result = detector.addFrame(frameWithShade(20), 80);
    QCOMPARE(result.outcome, Outcome::Settled);
    QCOMPARE(result.strictSettleMs.value(), 27);
}

void tst_FrameSettleDetector::caretBeforeScrollKeepsGracePeriod()
{
    // A caret blink before the application reacts is not the scroll starting.
    FrameSettleDetector detector;
    const QImage start = frameWithShade(0);
    detector.start(start);
    QCOMPARE(detector.addFrame(withCaret(start, true), 20).outcome, Outcome::Pending);
    QCOMPARE(detector.addFrame(withCaret(start, true), 100).outcome, Outcome::Pending);
    QVERIFY(!detector.result().motionObserved);
    QCOMPARE(detector.addFrame(frameWithShade(30), 150).outcome, Outcome::Pending);
    const auto result = detector.addFrame(frameWithShade(30), 210);
    QCOMPARE(result.outcome, Outcome::Settled);
    QCOMPARE(result.strictSettleMs.value(), 150);
    QVERIFY(result.motionObserved);
}

void tst_FrameSettleDetector::caretOnlyIsNotMotion()
{
    FrameSettleDetector::Config config;
    config.noMotionGraceMs = 100;
    FrameSettleDetector detector(config);
    const QImage start = frameWithShade(0);
    detector.start(start);
    detector.addFrame(withCaret(start, true), 20);
    const auto result = detector.addFrame(withCaret(start, true), 150);
    QCOMPARE(result.outcome, Outcome::Settled);
    QVERIFY(!result.motionObserved);
}

void tst_FrameSettleDetector::countsChangedTilesIncludingPartialEdges()
{
    // 100x70 with 32px tiles is a 4x3 grid whose last row and column are partial.
    QImage a(100, 70, QImage::Format_RGB32);
    a.fill(Qt::white);
    QImage b = a.copy();
    b.setPixel(99, 69, qRgb(1, 2, 3));
    b.setPixel(0, 0, qRgb(1, 2, 3));
    int total = 0;
    QCOMPARE(FrameSettleDetector::countChangedTiles(a, b, 32, &total), 2);
    QCOMPARE(total, 12);
    QCOMPARE(FrameSettleDetector::countChangedTiles(a, a, 32), 0);
    QCOMPARE(FrameSettleDetector::countChangedTiles(a, QImage(10, 10, QImage::Format_RGB32), 32, &total), 1);
}

QTEST_GUILESS_MAIN(tst_FrameSettleDetector)
#include "tst_FrameSettleDetector.moc"
