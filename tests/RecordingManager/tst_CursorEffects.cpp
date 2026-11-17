#include <QtTest>

#include <QImage>
#include <QPainter>
#include <cmath>

#include "recording/CursorEffects.h"
#include "recording/RecordingFrameFilter.h"

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
    void springSteadyMotion();
    void springEventsAndSeeks();
    void springCutsDoNotLookAhead();
    void springPrecomputationBudget();
    void springJumpAndVisibility();
    void springJitterPreservesShapeBoundaries();
    void frameFilterKeepsStylesAndMasks();
    void legacyClickRecipesMatchOriginalFrames();
    void clickStylesAreDeterministicAndClipped();
    void bounceIsContinuousAndCutBounded();
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
    void advancedSettingsRoundTrip();
    void hiddenRangesNormalize();
    void idleHideFadesAndWakesBeforeActivity();
    void idleHideKeepsClicksVisible();
    void motionBlurTrailsOnlyAMovingPointer();
    void stillEndHoldsTheLastPosition();
    void hiddenRangesHidePointerAndRipples();
    void advancedEffectsDrawThroughCrop();
    void nothingReachesAcrossACut();
};

void TestCursorEffects::springSteadyMotion()
{
    CursorTimeline t;
    for (qint64 ms = 0; ms <= 4000; ms += 8)
        t.samples.push_back({ms, QPoint(int(ms), int(ms / 2)), true, 0});
    for (auto level : {CursorEffectSettings::Smoothing::Low, CursorEffectSettings::Smoothing::Medium,
                       CursorEffectSettings::Smoothing::High}) {
        CursorEffectSettings s;
        s.smoothing = level;
        CursorEffectRenderer renderer(t, s);
        // Baseline lowpass at 1000 px/s: Low 43.5369, Medium 82.8739,
        // High 141.951 source px behind at 3000 ms (8 ms samples).
        for (qint64 ms = 2000; ms < 3000; ms += 8) {
            const QPointF error = *renderer.pointerPosition(ms) - QPointF(ms, ms / 2);
            QVERIFY2(std::hypot(error.x(), error.y()) < 2.0, qPrintable(QString::number(error.x())));
        }
    }
    // No version key: existing recipes deliberately adopt the new model.
    const auto legacy = CursorEffectSettings::fromJson({{QStringLiteral("smoothing"), QStringLiteral("high")}});
    QVERIFY(qAbs(CursorEffectRenderer(t, legacy).pointerPosition(3000)->x() - 3000) < 2.0);
}

void TestCursorEffects::springEventsAndSeeks()
{
    CursorTimeline t;
    t.samples = {{0, QPoint(0, 0), true, 0}, {1000, QPoint(100, 50), true, 0},
                 {1100, QPoint(100, 50), true, 0}, {1200, QPoint(110, 55), true, 0},
                 {1600, QPoint(200, 100), true, 0}, {2000, QPoint(3840, 2160), true, 0},
                 {3000, QPoint(3840, 2160), true, 0}};
    t.buttons = {{1003, Qt::LeftButton, true, QPoint(100, 50), false},
                 {1051, Qt::LeftButton, false, QPoint(100, 50), false},
                 {1157, Qt::LeftButton, true, QPoint(110, 55), false},
                 {1631, Qt::LeftButton, false, QPoint(200, 100), false}};
    CursorEffectSettings s;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    CursorEffectRenderer r(t, s);
    for (const auto& e : t.buttons) QCOMPARE(*r.pointerPosition(e.tMs), QPointF(e.position));
    std::vector<QPointF> forward;
    for (qint64 ms = 0; ms < 4000; ++ms) {
        const auto p = *r.pointerPosition(ms);
        QVERIFY(p.x() >= 0 && p.x() <= 3840);
        QVERIFY(p.y() >= 0 && p.y() <= 2160);
        forward.push_back(p);
    }
    for (int i = 3999; i >= 0; --i) QCOMPARE(*r.pointerPosition(i), forward[size_t(i)]);
    CursorEffectRenderer rebuilt(t, s);
    for (int i = 0; i < 4000; ++i) {
        const int ms = (i * 997) % 4000;
        QCOMPARE(*rebuilt.pointerPosition(ms), forward[size_t(ms)]);
    }
}

void TestCursorEffects::springCutsDoNotLookAhead()
{
    CursorTimeline a;
    a.samples = {{0, QPoint(10, 20), true, 0}, {900, QPoint(10, 20), true, 0},
                 {1100, QPoint(3000, 2000), true, 0}, {2100, QPoint(30, 40), true, 0}};
    a.buttons = {{1100, Qt::LeftButton, true, QPoint(3000, 2000), false},
                 {1200, Qt::LeftButton, false, QPoint(3000, 2000), false}};
    CursorEffectSettings s;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    CursorEffectRenderer r(a, s);
    r.setTimeline(TimelineMap({{0, 1000, 1}, {2100, 2600, 1}}));
    for (qint64 ms = 0; ms < 1000; ms += 7) QCOMPARE(*r.pointerPosition(ms), QPointF(10, 20));
    QCOMPARE(*r.pointerPosition(2100), QPointF(30, 40));
    // Reapplying the identical map must not alter any result.
    const auto before = r.pointerPosition(2300);
    r.setTimeline(TimelineMap({{0, 1000, 1}, {2100, 2600, 1}}));
    QCOMPARE(r.pointerPosition(2300), before);
}

void TestCursorEffects::springJumpAndVisibility()
{
    CursorTimeline t;
    t.samples = {{0, QPoint(0, 0), true, 0}, {999, QPoint(0, 0), true, 0},
                 {1000, QPoint(3840, 2160), true, 1}, {3000, QPoint(3840, 2160), true, 1}};
    CursorEffectSettings s;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    CursorEffectRenderer r(t, s);
    double last = 0;
    for (qint64 ms = 0; ms <= 3000; ++ms) {
        const auto p = *r.pointerPosition(ms);
        QVERIFY(p.x() >= last && p.x() <= 3840);
        last = p.x();
    }
    t.samples.insert(t.samples.begin() + 3, {2000, QPoint(3840, 2160), false, 1});
    CursorEffectRenderer hidden(t, s);
    QVERIFY(!hidden.pointerPosition(2500));
    QVERIFY(hidden.pointerPosition(3000));
    s.stillEndMs = 500;
    s.hiddenRanges = {{2100, 2300}};
    CursorEffectRenderer frozen(t, s, 2000);
    QCOMPARE(frozen.pointerPosition(1999), frozen.pointerPosition(1500));
}

void TestCursorEffects::springJitterPreservesShapeBoundaries()
{
    CursorTimeline t;
    t.samples = {{0, QPoint(0, 0), true, 0}, {950, QPoint(0, 0), true, 0},
                 {1000, QPoint(1, 0), true, 0}, {1030, QPoint(0, 0), true, 0},
                 {2000, QPoint(0, 0), true, 0}};
    CursorEffectSettings s;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    CursorEffectRenderer filtered(t, s);
    QCOMPARE(*CursorEffectRenderer(t, {}).pointerPosition(1000), QPointF(1, 0));
    for (int ms = 900; ms < 1200; ++ms) QCOMPARE(*filtered.pointerPosition(ms), QPointF(0, 0));
    // The same tiny move at a shape transition is an endpoint to preserve.
    t.samples[2].shapeId = t.samples[3].shapeId = t.samples[4].shapeId = 1;
    CursorEffectRenderer semantic(t, s);
    QVERIFY(semantic.pointerPosition(1000)->x() > 0.0);
}

void TestCursorEffects::springPrecomputationBudget()
{
    CursorTimeline t;
    for (int i = 0; i <= 72000; ++i)
        t.samples.push_back({qint64(i) * 1000 / 120, QPoint(i % 3840, i % 2160), true, 0});
    CursorEffectSettings s;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    QElapsedTimer timer;
    timer.start();
    CursorEffectRenderer r(t, s);
    const double elapsedMs = timer.nsecsElapsed() / 1e6;
    qInfo("10-minute 120 Hz spring precomputation: %.3f ms", elapsedMs);
#ifdef NDEBUG
    QVERIFY(elapsedMs < 50.0);
#else
    QVERIFY(elapsedMs < 1000.0);
#endif
    QVERIFY(r.pointerPosition(599000).has_value());
}

void TestCursorEffects::settingsRoundTrip()
{
    CursorEffectSettings s;
    s.showCursor = false;
    s.scale = 1.5;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    s.clickStyle = SnapTray::CursorEffectSettings::ClickStyle::Ring;
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
    // Button events are authoritative even when the sampled path differs.
    QCOMPARE(*renderer.pointerPosition(300), QPointF(t.buttons[0].position));
    QCOMPARE(*renderer.pointerPosition(340), QPointF(t.buttons[1].position));
}

void TestCursorEffects::rippleAnchoredToThePress()
{
    const CursorTimeline t = timeline();
    CursorEffectSettings s;
    s.showCursor = false;
    s.clickStyle = SnapTray::CursorEffectSettings::ClickStyle::Ring;
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
    s.clickStyle = SnapTray::CursorEffectSettings::ClickStyle::Ring;
    s.motionBlur = CursorEffectSettings::MotionBlur::High; // the most draws per frame
    s.idleHideMs = 1000;
    s.stillEndMs = 500;
    CursorEffectRenderer renderer(t, s, 9600);
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

namespace {

// Moves right 10 px every 16 ms until 160 ms, rests until 3000 ms, then
// moves again; a click at 2000 ms.
CursorTimeline restingTimeline()
{
    CursorTimeline t;
    t.info.frameSize = QSize(400, 100);
    t.info.clicksAvailable = true;
    for (int i = 0; i <= 10; ++i) t.samples.push_back({i * 16, QPoint(20 + i * 10, 50), true, 0});
    t.samples.push_back({3000, QPoint(200, 50), true, 0});
    t.samples.push_back({3016, QPoint(220, 50), true, 0});
    return t;
}

} // namespace

void TestCursorEffects::advancedSettingsRoundTrip()
{
    CursorEffectSettings s;
    s.idleHideMs = 2000;
    s.motionBlur = CursorEffectSettings::MotionBlur::Medium;
    s.stillEndMs = 500;
    s.hiddenRanges = {{100, 400}, {1000, 2500}};
    QCOMPARE(CursorEffectSettings::fromJson(s.toJson()), s);
    // Older recipes (no #186 keys) keep every new effect off.
    const auto older = CursorEffectSettings::fromJson({{QStringLiteral("showCursor"), true}});
    QCOMPARE(older.idleHideMs, 0);
    QCOMPARE(older.motionBlur, CursorEffectSettings::MotionBlur::Off);
    QCOMPARE(older.stillEndMs, 0);
    QVERIFY(older.hiddenRanges.empty());
    // Out of range is clamped.
    const auto clamped = CursorEffectSettings::fromJson(
        {{QStringLiteral("idleHideMs"), -5.0}, {QStringLiteral("stillEndMs"), 1e9}});
    QCOMPARE(clamped.idleHideMs, 0);
    QCOMPARE(clamped.stillEndMs, CursorEffectSettings::kMaxStillEndMs);
    // Remembered preferences never carry one recording's ranges.
    QVERIFY(s.preferences().hiddenRanges.empty());
    QCOMPARE(s.preferences().idleHideMs, 2000);
    QVERIFY(!s.preferences().toJson().contains(QStringLiteral("hiddenRanges")));
}

void TestCursorEffects::hiddenRangesNormalize()
{
    const auto ranges = CursorEffectSettings::normalizedRanges(
        {{500, 900}, {100, 300}, {250, 400}, {2000, 2050}, {-10, 50}, {900, 1000}});
    // Sorted, overlapping and touching merged, too short or negative dropped.
    QCOMPARE(int(ranges.size()), 2);
    QCOMPARE(ranges[0], (CursorHiddenRange{100, 400}));
    QCOMPARE(ranges[1], (CursorHiddenRange{500, 1000}));
    CursorEffectSettings s;
    s.hiddenRanges = ranges;
    QVERIFY(s.hiddenAt(100));
    QVERIFY(!s.hiddenAt(400)); // end is exclusive
    QVERIFY(s.hiddenAt(999));
    QVERIFY(!s.hiddenAt(1000));
    QVERIFY(!s.hiddenAt(450));
}

void TestCursorEffects::idleHideFadesAndWakesBeforeActivity()
{
    CursorTimeline t = restingTimeline();
    t.buttons.clear();
    CursorEffectSettings s;
    s.idleHideMs = 1000;
    const CursorEffectRenderer renderer(t, s);
    QCOMPARE(renderer.pointerOpacity(100), 1.0);
    // Last movement at 160 ms: visible for 1 s, then a 250 ms fade.
    QCOMPARE(renderer.pointerOpacity(1160), 1.0);
    QVERIFY(qAbs(renderer.pointerOpacity(1160 + CursorEffectRenderer::kIdleFadeMs / 2) - 0.5) < 0.01);
    QCOMPARE(renderer.pointerOpacity(1500), 0.0);
    // Back in before it moves at 3000 ms, fully there when it does.
    QCOMPARE(renderer.pointerOpacity(3000 - CursorEffectRenderer::kIdleFadeMs), 0.0);
    QVERIFY(qAbs(renderer.pointerOpacity(3000 - CursorEffectRenderer::kIdleFadeMs / 2) - 0.5) < 0.01);
    QCOMPARE(renderer.pointerOpacity(3000), 1.0);
    // A function of time only: any order, any renderer, same answer.
    const double a = renderer.pointerOpacity(1200);
    renderer.pointerOpacity(5000);
    renderer.pointerOpacity(0);
    QCOMPARE(renderer.pointerOpacity(1200), a);
    QCOMPARE(CursorEffectRenderer(t, s).pointerOpacity(1200), a);
    // Hidden pointers are not drawn.
    const QImage base = blank(QSize(400, 100));
    QImage frame = base;
    renderer.paint(frame, 2000, {QRect(), QSize(400, 100), QSize(400, 100)});
    QCOMPARE(frame, base);
    // Off: always fully visible.
    QCOMPARE(CursorEffectRenderer(t, {}).pointerOpacity(2000), 1.0);
}

void TestCursorEffects::idleHideKeepsClicksVisible()
{
    CursorTimeline t = restingTimeline();
    t.buttons = {{2000, Qt::LeftButton, true, QPoint(120, 50), false},
                 {2040, Qt::LeftButton, false, QPoint(120, 50), false}};
    CursorEffectSettings s;
    s.idleHideMs = 1000;
    s.clickStyle = SnapTray::CursorEffectSettings::ClickStyle::Ring;
    const CursorEffectRenderer renderer(t, s);
    // A click is activity: the pointer is there at the press without moving.
    QCOMPARE(renderer.pointerOpacity(2000), 1.0);
    QVERIFY(renderer.pointerOpacity(2000 - CursorEffectRenderer::kIdleFadeMs / 2) > 0.0);
    // Ripples are drawn whatever the pointer's opacity.
    CursorEffectSettings ripplesOnly = s;
    ripplesOnly.showCursor = false;
    const QImage base = blank(QSize(400, 100));
    QImage frame = base;
    CursorEffectRenderer(t, ripplesOnly).paint(frame, 2100, {QRect(), QSize(400, 100), QSize(400, 100)});
    QVERIFY(differingPixels(frame, base) > 0);
}

void TestCursorEffects::motionBlurTrailsOnlyAMovingPointer()
{
    const CursorTimeline t = restingTimeline();
    const CursorFrameMapping mapping{QRect(), QSize(400, 100), QSize(400, 100)};
    CursorEffectSettings blurred;
    blurred.motionBlur = CursorEffectSettings::MotionBlur::High;
    const QImage base = blank(QSize(400, 100));
    QImage sharp = base, trail = base;
    CursorEffectRenderer(t, {}).paint(sharp, 120, mapping);
    CursorEffectRenderer renderer(t, blurred);
    renderer.paint(trail, 120, mapping);
    // Moving: ghosts behind (left of) the pointer at x=95.
    QVERIFY(differingPixels(trail, base) > differingPixels(sharp, base));
    bool ghostLeft = false;
    for (int x = 40; x < 90; ++x) ghostLeft |= trail.pixel(x, 60) != base.pixel(x, 60);
    QVERIFY(ghostLeft);
    // Resting: exactly the sharp pointer.
    QImage restingSharp = base, restingBlur = base;
    CursorEffectRenderer(t, {}).paint(restingSharp, 1000, mapping);
    renderer.paint(restingBlur, 1000, mapping);
    QCOMPARE(restingBlur, restingSharp);
    // Same time, same pixels, whatever was drawn before.
    renderer.paint(restingBlur, 5000, mapping);
    QImage again = base;
    renderer.paint(again, 120, mapping);
    QCOMPARE(again, trail);
}

void TestCursorEffects::stillEndHoldsTheLastPosition()
{
    const CursorTimeline t = restingTimeline();
    CursorEffectSettings s;
    s.stillEndMs = 500;
    // The clip ends at 3200 ms: from 2700 ms the pointer stays where it was.
    const CursorEffectRenderer renderer(t, s, 3200);
    QCOMPARE(*renderer.pointerPosition(3100), QPointF(120, 50));
    QCOMPARE(*renderer.pointerPosition(2700), QPointF(120, 50));
    QCOMPARE(*renderer.pointerPosition(100), *CursorEffectRenderer(t, {}).pointerPosition(100));
    // Unknown clip end: off.
    QCOMPARE(*CursorEffectRenderer(t, s).pointerPosition(3100), QPointF(220, 50));
    // With parts cut out (PR #199 review): the hold starts that long before
    // the exported end, at a kept time, never in a cut part. Keep 0-1000 and
    // 4000-5000 with a 1.5 s hold: the exported 2 s hold from output 500 ms,
    // which shows source 500 ms (not 3500 ms, inside the cut).
    {
        CursorTimeline moving;
        moving.info.frameSize = QSize(1000, 100);
        for (qint64 ms = 0; ms <= 5000; ms += 100) moving.samples.push_back({ms, QPoint(int(ms / 10), 50), true, 0});
        CursorEffectSettings hold;
        hold.stillEndMs = 1500;
        CursorEffectRenderer cut(moving, hold, 5000);
        cut.setTimeline(TimelineMap({{0, 1000, 1.0}, {4000, 5000, 1.0}}));
        QCOMPARE(*cut.pointerPosition(300), QPointF(30, 50));   // before the hold: moving
        QCOMPARE(*cut.pointerPosition(600), QPointF(50, 50));   // held at source 500 ms
        QCOMPARE(*cut.pointerPosition(4500), QPointF(50, 50));  // the last part too
        QCOMPARE(*cut.pointerPosition(4999), QPointF(50, 50));
        // Longer than everything kept: held at the first kept moment.
        hold.stillEndMs = 3000;
        CursorEffectRenderer allHeld(moving, hold, 5000);
        allHeld.setTimeline(TimelineMap({{0, 1000, 1.0}, {4000, 5000, 1.0}}));
        QCOMPARE(*allHeld.pointerPosition(4500), QPointF(0, 50));
    }
    // A clip shorter than the interval (PR #196 review): the pointer stays at
    // its first sample instead of vanishing.
    CursorEffectSettings longHold;
    longHold.stillEndMs = 2000;
    const CursorEffectRenderer shortClip(t, longHold, 1000);
    for (const qint64 ms : {qint64(0), qint64(100), qint64(500), qint64(999)}) {
        const auto at = shortClip.pointerPosition(ms);
        QVERIFY2(at.has_value(), qPrintable(QString::number(ms)));
        QCOMPARE(*at, QPointF(20, 50));
    }
    // With idle hiding, the movement after the hold does not wake the pointer.
    s.idleHideMs = 1000;
    const CursorEffectRenderer idle(t, s, 3200);
    QCOMPARE(idle.pointerOpacity(3100), 0.0);
    QCOMPARE(idle.pointerOpacity(2700 - CursorEffectRenderer::kIdleFadeMs / 2), 0.0);
}

void TestCursorEffects::hiddenRangesHidePointerAndRipples()
{
    CursorTimeline t = timeline();
    CursorEffectSettings s;
    s.clickStyle = SnapTray::CursorEffectSettings::ClickStyle::Ring;
    s.hiddenRanges = {{250, 500}};
    const CursorEffectRenderer renderer(t, s);
    QVERIFY(renderer.pointerPosition(200));
    QVERIFY(!renderer.pointerPosition(300));
    QVERIFY(renderer.pointerPosition(500));
    const QImage base = blank();
    QImage frame = base;
    renderer.paint(frame, 400, {QRect(), QSize(200, 100), QSize(200, 100)});
    QCOMPARE(frame, base);
}

void TestCursorEffects::advancedEffectsDrawThroughCrop()
{
    // Blur and idle hiding go through the same crop mapping as the pointer:
    // the cropped frame equals the matching part of the full frame.
    const CursorTimeline t = restingTimeline();
    CursorEffectSettings s;
    s.motionBlur = CursorEffectSettings::MotionBlur::High;
    s.idleHideMs = 1000;
    const CursorEffectRenderer renderer(t, s);
    const QImage base = blank(QSize(400, 100));
    QImage full = base;
    renderer.paint(full, 120, {QRect(), QSize(400, 100), QSize(400, 100)});
    QImage cropped = base.copy(0, 0, 200, 100);
    renderer.paint(cropped, 120, {QRect(0, 0, 200, 100), QSize(400, 100), QSize(200, 100)});
    QCOMPARE(cropped, full.copy(0, 0, 200, 100));
}

void TestCursorEffects::nothingReachesAcrossACut()
{
    // Kept: 0-1000 and 2000-3000. In the cut part the pointer jumps to
    // (150,50) at 1990 ms and clicks at 1950 ms.
    CursorTimeline t;
    t.info.frameSize = QSize(400, 100);
    t.info.clicksAvailable = true;
    t.samples = {{0, QPoint(20, 50), true, 0}, {1990, QPoint(150, 50), true, 0}};
    t.buttons = {{1950, Qt::LeftButton, true, QPoint(20, 50), false},
                 {1960, Qt::LeftButton, false, QPoint(20, 50), false}};
    const TimelineMap kept({{0, 1000, 1.0}, {2000, 3000, 1.0}});
    const CursorFrameMapping mapping{QRect(), QSize(400, 100), QSize(400, 100)};

    CursorEffectSettings s;
    s.smoothing = CursorEffectSettings::Smoothing::High;
    CursorEffectRenderer whole(t, s);
    CursorEffectRenderer cut(t, s);
    cut.setTimeline(kept);
    // Without the cut the pointer would still be gliding over from (20,50).
    QVERIFY(whole.pointerPosition(2010)->x() < 140.0);
    QCOMPARE(*cut.pointerPosition(2010), QPointF(150, 50));

    // The click in the cut part leaves no ripple after the cut.
    CursorEffectSettings ripples;
    ripples.showCursor = false;
    ripples.clickStyle = SnapTray::CursorEffectSettings::ClickStyle::Ring;
    const QImage base = blank(QSize(400, 100));
    QImage withoutCut = base, withCut = base;
    CursorEffectRenderer(t, ripples).paint(withoutCut, 2050, mapping);
    CursorEffectRenderer rippleCut(t, ripples);
    rippleCut.setTimeline(kept);
    rippleCut.paint(withCut, 2050, mapping);
    QVERIFY(differingPixels(withoutCut, base) > 0);
    QCOMPARE(differingPixels(withCut, base), 0);

    // No motion blur trail from where the pointer was before the cut.
    CursorEffectSettings blur;
    blur.motionBlur = CursorEffectSettings::MotionBlur::High;
    CursorEffectRenderer blurCut(t, blur);
    blurCut.setTimeline(kept);
    QImage sharp = base, blurred = base;
    CursorEffectRenderer(t, {}).paint(sharp, 2005, mapping);
    blurCut.paint(blurred, 2005, mapping);
    QCOMPARE(blurred, sharp);
}

void TestCursorEffects::frameFilterKeepsStylesAndMasks()
{
    auto t = std::make_shared<CursorTimeline>(timeline());
    const TimelineMap map({{0, 1000, 1}});
    for (int style = 1; style <= 4; ++style) {
        PreviewEditState state;
        state.cursor.clickStyle = CursorEffectSettings::ClickStyle(style);
        state.cursor.clickColor = QColor("#4dabf7");
        state.cursor.pressBounce = CursorEffectSettings::PressBounce::High;
        state.redaction.masks.push_back({QRect(100, 40, 40, 40), 0, -1, RedactionMask::Style::Solid});
        const auto filter = recordingFrameFilter(state, t->info.frameSize, map, t);
        QImage actual = blank();
        filter(actual, 410, 420);
        QImage expected = blank();
        state.redaction.apply(expected, 410, QRectF(QPointF(), t->info.frameSize), 610);
        CursorEffectRenderer renderer(*t, state.cursor, 1000);
        renderer.setTimeline(map);
        renderer.paint(expected, 410, {QRect(), t->info.frameSize, expected.size()});
        QCOMPARE(actual, expected); // shared MP4/GIF/WebP/poster filter uses the same effects
        QImage differentSecret = blank();
        { QPainter painter(&differentSecret); painter.fillRect(QRect(100, 40, 40, 40), Qt::magenta); }
        filter(differentSecret, 410, 420);
        QCOMPARE(differentSecret, actual); // even Spotlight cannot reveal masked pixels
    }
}

void TestCursorEffects::legacyClickRecipesMatchOriginalFrames()
{
    const auto t = timeline();
    const auto off = CursorEffectSettings::fromJson({{QStringLiteral("clickRipple"), false}});
    QCOMPARE(off.clickStyle, CursorEffectSettings::ClickStyle::Off);
    const auto old = CursorEffectSettings::fromJson({{QStringLiteral("clickRipple"), true},
                                                     {QStringLiteral("showCursor"), false}});
    QCOMPARE(old.clickStyle, CursorEffectSettings::ClickStyle::Ring);
    QCOMPARE(old.pressBounce, CursorEffectSettings::PressBounce::Off);
    // Independent reference for the original main-branch ring, for every frame.
    for (int age = 0; age < 450; age += 7) {
        QImage reference = blank();
        QPainter p(&reference);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const double progress = double(age) / 450;
        const double fade = 1.0 - progress;
        const double radius = 6.0 + 22.0 * (1.0 - std::pow(1.0 - progress, 3.0));
        p.setPen(QPen(QColor(0, 0, 0, int(220 * fade * 0.5)), 4.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(120, 60), radius, radius);
        p.setPen(QPen(QColor(255, 255, 255, int(220 * fade)), 3.0));
        p.setBrush(QColor(255, 255, 255, int(70 * fade)));
        p.drawEllipse(QPointF(120, 60), radius, radius);
        p.end();
        QImage actual = blank();
        CursorEffectRenderer(t, old).paint(actual, 300 + age, {QRect(), t.info.frameSize, actual.size()});
        QCOMPARE(actual, reference);
    }
}

void TestCursorEffects::clickStylesAreDeterministicAndClipped()
{
    using Style = CursorEffectSettings::ClickStyle;
    for (Style style : {Style::Ring, Style::Ripple, Style::Spotlight, Style::Burst}) {
        auto t = timeline();
        CursorEffectSettings s;
        s.showCursor = false;
        s.clickStyle = style;
        s.scale = 3.0;
        s.clickColor = QColor("#ffd43b");
        s.rightClickColor = QColor("#4dabf7");
        s.separateRightClickColor = true;
        s.pressBounce = CursorEffectSettings::PressBounce::High;
        QCOMPARE(CursorEffectSettings::fromJson(s.toJson()), s);
        const CursorFrameMapping mapping{QRect(100, 50, 100, 50), t.info.frameSize, QSize(200, 100)};
        CursorEffectRenderer r(t, s);
        auto first = blank();
        r.paint(first, 410, mapping);
        QVERIFY(differingPixels(first, blank()) > 0);
        auto other = blank();
        r.paint(other, 800, mapping);
        auto again = blank();
        r.paint(again, 410, mapping);
        QCOMPARE(first, again);
        t.buttons.front().button = Qt::RightButton;
        auto right = blank();
        CursorEffectRenderer(t, s).paint(right, 410, mapping);
        QVERIFY(differingPixels(right, first) > 0);
        r.setTimeline(TimelineMap({{0, 250, 1}, {350, 1000, 1}}));
        auto cut = blank();
        r.paint(cut, 410, mapping);
        QCOMPARE(cut, blank());
        // Effects may touch the edge but cannot paint beyond the output viewport.
        auto large = blank(QSize(300, 200));
        CursorEffectRenderer(t, s).paint(large, 410, mapping);
        QCOMPARE(large.pixel(220, 60), blank().pixel(0, 0));
        QCOMPARE(large.pixel(50, 120), blank().pixel(0, 0));
    }
}

void TestCursorEffects::bounceIsContinuousAndCutBounded()
{
    auto t = timeline();
    t.buttons = {{100, Qt::LeftButton, true, QPoint(120, 60), false},
                 {500, Qt::LeftButton, false, QPoint(120, 60), false},
                 {550, Qt::LeftButton, true, QPoint(120, 60), false},
                 {570, Qt::LeftButton, false, QPoint(120, 60), false}};
    CursorEffectSettings s;
    s.pressBounce = CursorEffectSettings::PressBounce::High;
    CursorEffectRenderer r(t, s);
    QCOMPARE(r.pointerBounceScale(99), 1.0);
    QCOMPARE(r.pointerBounceScale(100), 1.0);
    QCOMPARE(r.pointerBounceScale(210), 0.84);
    QCOMPARE(r.pointerBounceScale(499), 0.84); // hold a drag at its minimum
    QCOMPARE(r.pointerBounceScale(700), 1.0);
    for (int ms = 1; ms < 800; ++ms) {
        const double value = r.pointerBounceScale(ms);
        QVERIFY(value >= 0.84 && value <= 1.0);
        QVERIFY(std::abs(value - r.pointerBounceScale(ms - 1)) < 0.004);
    }
    r.setTimeline(TimelineMap({{0, 80, 1}, {400, 800, 1}}));
    QCOMPARE(r.pointerBounceScale(400), 1.0); // press was cut away
    QCOMPARE(r.pointerBounceScale(500), 1.0); // orphan release cannot bounce
    QVERIFY(r.pointerBounceScale(560) < 1.0);
    t.info.clicksAvailable = false;
    QCOMPARE(CursorEffectRenderer(t, s).pointerBounceScale(210), 1.0);
}

QTEST_MAIN(TestCursorEffects)
#include "tst_CursorEffects.moc"
