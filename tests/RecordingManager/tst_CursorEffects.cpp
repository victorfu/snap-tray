#include <QtTest>

#include <QImage>

#include "recording/CursorEffects.h"

using namespace SnapTray;

namespace {

QImage blank(QSize size = QSize(200, 100))
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(40, 80, 120));
    return image;
}

CursorTimeline timeline()
{
    CursorTimeline t;
    t.info.frameSize = QSize(200, 100);
    t.info.clicksAvailable = true;
    t.info.shapesAvailable = true;
    QImage shape(4, 4, QImage::Format_ARGB32_Premultiplied);
    shape.fill(Qt::red);
    t.shapes.push_back({1, QPoint(1, 1), shape});
    // A jump from (20,20) to (120,60) at 100 ms, a click at 300 ms.
    t.samples = {{0, QPoint(20, 20), true, 1}, {100, QPoint(120, 60), true, 1}};
    t.buttons = {{300, Qt::LeftButton, true, QPoint(120, 60), false},
                 {340, Qt::LeftButton, false, QPoint(120, 60), false}};
    return t;
}

int differingPixels(const QImage& a, const QImage& b)
{
    int count = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) count += a.pixel(x, y) != b.pixel(x, y) ? 1 : 0;
    return count;
}

} // namespace

class TestCursorEffects : public QObject
{
    Q_OBJECT

private slots:
    void settingsRoundTrip();
    void availabilityFollowsTheSidecar();
    void pointerDrawnAtHotspotThroughCrop();
    void defaultArrowWithoutShapes();
    void smoothingIsAFunctionOfTime();
    void clicksKeepTheRealPosition();
    void rippleAnchoredToThePress();
    void hiddenAndBeforeFirstSample();
    void disabledDrawsNothing();
    void costAt4K();
};

void TestCursorEffects::settingsRoundTrip()
{
    CursorEffectSettings s;
    s.showCursor = false;
    s.scale = 1.5;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    s.clickRipple = true;
    QCOMPARE(CursorEffectSettings::fromJson(s.toJson()), s);
    // Missing keys keep defaults; out-of-range sizes are clamped.
    const auto defaults = CursorEffectSettings::fromJson({{QStringLiteral("scale"), 99.0}});
    QVERIFY(defaults.showCursor);
    QCOMPARE(defaults.scale, CursorEffectSettings::kMaxScale);
    QCOMPARE(defaults.smoothing, CursorEffectSettings::Smoothing::Off);
}

void TestCursorEffects::availabilityFollowsTheSidecar()
{
    QVERIFY(!CursorEffectAvailability::of(nullptr).cursor);
    CursorTimeline t = timeline();
    auto a = CursorEffectAvailability::of(&t);
    QVERIFY(a.cursor && a.clicks);
    t.info.clicksAvailable = false;
    QVERIFY(!CursorEffectAvailability::of(&t).clicks);
    // The pointer is already in the video: drawing would double it.
    t.info.cursorInVideo = true;
    QVERIFY(!CursorEffectAvailability::of(&t).cursor);
    CursorTimeline empty;
    QVERIFY(!CursorEffectAvailability::of(&empty).cursor);
}

void TestCursorEffects::pointerDrawnAtHotspotThroughCrop()
{
    const CursorTimeline t = timeline();
    CursorEffectRenderer renderer(t, {});
    // Crop (100,50 100x50) shown at 2x: source (120,60) -> output (40,20).
    CursorFrameMapping mapping{QRect(100, 50, 100, 50), QSize(200, 100), QSize(200, 100)};
    QCOMPARE(mapping.toOutput(QPointF(120, 60)), QPointF(40, 20));
    QImage frame = blank();
    renderer.paint(frame, 200, mapping);
    // The 4x4 shape scaled 2x, hotspot (1,1) -> image from (38,18) to (46,26).
    QCOMPARE(frame.pixelColor(42, 22), QColor(Qt::red));
    QCOMPARE(frame.pixelColor(37, 17), QColor(40, 80, 120));
    QCOMPARE(frame.pixelColor(47, 27), QColor(40, 80, 120));

    // A user size of 2x doubles it again.
    CursorEffectSettings big;
    big.scale = 2.0;
    QImage bigFrame = blank();
    CursorEffectRenderer(t, big).paint(bigFrame, 200, mapping);
    QCOMPARE(bigFrame.pixelColor(36 + 1, 16 + 1), QColor(Qt::red));
}

void TestCursorEffects::defaultArrowWithoutShapes()
{
    CursorTimeline t = timeline();
    t.shapes.clear();
    t.info.shapesAvailable = false;
    t.info.pixelRatio = 1.0;
    QImage frame = blank();
    CursorEffectRenderer(t, {}).paint(frame, 200, {QRect(), QSize(200, 100), QSize(200, 100)});
    // The arrow's body lies below-right of the tip at (120,60).
    QCOMPARE(frame.pixelColor(122, 70), QColor(Qt::black));
    QCOMPARE(frame.pixelColor(110, 50), QColor(40, 80, 120));
}

void TestCursorEffects::smoothingIsAFunctionOfTime()
{
    const CursorTimeline t = timeline();
    CursorEffectSettings s;
    s.smoothing = CursorEffectSettings::Smoothing::Medium;
    CursorEffectRenderer renderer(t, s);
    // Right after the jump the drawn pointer lags, then converges.
    const QPointF early = *renderer.pointerPosition(120);
    QVERIFY(early.x() > 20 && early.x() < 120);
    // Six time constants later (and clear of the click's snap window) it has arrived.
    QVERIFY(qAbs(renderer.pointerPosition(600)->x() - 120) < 1.0);
    // Monotonic toward the target, no overshoot.
    double last = 0;
    for (qint64 ms = 100; ms <= 240; ms += 10) {
        const double x = renderer.pointerPosition(ms)->x();
        QVERIFY(x >= last && x <= 120.0);
        last = x;
    }
    // Same time, same answer, in any order (seek, backward playback).
    const QPointF a = *renderer.pointerPosition(130);
    renderer.pointerPosition(5000);
    renderer.pointerPosition(0);
    QCOMPARE(*CursorEffectRenderer(t, s).pointerPosition(130), a);
    QCOMPARE(*renderer.pointerPosition(130), a);
    // Off: the recorded position exactly.
    QCOMPARE(*CursorEffectRenderer(t, {}).pointerPosition(120), QPointF(120, 60));
}

void TestCursorEffects::clicksKeepTheRealPosition()
{
    CursorTimeline t = timeline();
    // A fast move that ends exactly on a click.
    t.samples = {{0, QPoint(0, 0), true, 1}, {290, QPoint(150, 80), true, 1}};
    CursorEffectSettings s;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    CursorEffectRenderer renderer(t, s);
    // Without the snap, High smoothing would still be far behind at the press.
    QCOMPARE(*renderer.pointerPosition(300), QPointF(150, 80));
    QCOMPARE(*renderer.pointerPosition(340), QPointF(150, 80));
}

void TestCursorEffects::rippleAnchoredToThePress()
{
    const CursorTimeline t = timeline();
    CursorEffectSettings s;
    s.showCursor = false;
    s.clickRipple = true;
    CursorEffectRenderer renderer(t, s);
    const CursorFrameMapping mapping{QRect(), QSize(200, 100), QSize(200, 100)};
    const QImage base = blank();
    QImage before = base, during = base, after = base;
    renderer.paint(before, 299, mapping);
    renderer.paint(during, 400, mapping);
    renderer.paint(after, 300 + CursorEffectRenderer::kRippleDurationMs, mapping);
    QCOMPARE(differingPixels(before, base), 0);
    QVERIFY(differingPixels(during, base) > 0);
    QCOMPARE(differingPixels(after, base), 0);
    // Centered on the press: pixel centres 105.5 and 134.5 are both 14.5 from x=120.
    QCOMPARE(during.pixel(120 - 15, 60), during.pixel(120 + 14, 60));
    // Same time, same pixels.
    QImage again = base;
    renderer.paint(again, 400, mapping);
    QCOMPARE(again, during);
    // Synthetic releases (pause/stop) never ripple, and neither do recordings without clicks.
    CursorTimeline noClicks = t;
    noClicks.info.clicksAvailable = false;
    QImage none = base;
    CursorEffectRenderer(noClicks, s).paint(none, 400, mapping);
    QCOMPARE(differingPixels(none, base), 0);
}

void TestCursorEffects::hiddenAndBeforeFirstSample()
{
    CursorTimeline t = timeline();
    t.samples.front().tMs = 50;
    t.samples.push_back({500, QPoint(10, 10), false, 1});
    CursorEffectRenderer renderer(t, {});
    QVERIFY(!renderer.pointerPosition(10));
    QVERIFY(renderer.pointerPosition(60));
    QVERIFY(!renderer.pointerPosition(600));
}

void TestCursorEffects::disabledDrawsNothing()
{
    const CursorTimeline t = timeline();
    CursorEffectSettings off;
    off.showCursor = false;
    QVERIFY(!off.drawsAnything());
    const QImage base = blank();
    QImage frame = base;
    CursorEffectRenderer(t, off).paint(frame, 200, {QRect(), QSize(200, 100), QSize(200, 100)});
    QCOMPARE(frame, base);
}

void TestCursorEffects::costAt4K()
{
    // Ten seconds of 60 Hz motion with clicks, every effect on, drawn on 4K frames.
    CursorTimeline t;
    t.info.frameSize = QSize(3840, 2160);
    t.info.pixelRatio = 2.0;
    t.info.clicksAvailable = true;
    t.info.shapesAvailable = true;
    QImage shape(64, 64, QImage::Format_ARGB32_Premultiplied);
    shape.fill(Qt::black);
    t.shapes.push_back({1, QPoint(4, 4), shape});
    for (int i = 0; i < 600; ++i) t.samples.push_back({i * 16, QPoint(100 + i * 5, 100 + (i * 7) % 1900), true, 1});
    for (int i = 0; i < 20; ++i) {
        t.buttons.push_back({i * 500, Qt::LeftButton, true, QPoint(400, 400), false});
        t.buttons.push_back({i * 500 + 80, Qt::LeftButton, false, QPoint(400, 400), false});
    }
    CursorEffectSettings s;
    s.scale = 2.0;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    s.clickRipple = true;
    CursorEffectRenderer renderer(t, s);
    QImage frame(3840, 2160, QImage::Format_RGB32);
    frame.fill(Qt::gray);
    constexpr int kFrames = 60;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < kFrames; ++i) {
        renderer.paint(frame, 1000 + i * 33, {QRect(), t.info.frameSize, frame.size()});
    }
    const double msPerFrame = timer.nsecsElapsed() / 1e6 / kFrames;
    qInfo("pointer effects on a 4K frame: %.2f ms per frame", msPerFrame);
    // Small next to decoding and encoding a 4K frame; a loose bound for slow CI.
    QVERIFY(msPerFrame < 25.0);
}

QTEST_MAIN(TestCursorEffects)
#include "tst_CursorEffects.moc"
