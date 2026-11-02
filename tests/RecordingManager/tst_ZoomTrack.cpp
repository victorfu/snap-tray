#include <QtTest>

#include "recording/CursorEffects.h"
#include "recording/ZoomTrack.h"

#include <QFont>
#include <QPainter>

using namespace SnapTray;

namespace {

const QRectF kBounds(0, 0, 1920, 1080);

// A timeline at 60 Hz that rests at `p` and clicks at the given times there.
struct Script {
    CursorTimeline timeline;
    Script() { timeline.info.frameSize = QSize(1920, 1080); timeline.info.clicksAvailable = true; }
    void move(qint64 t, QPoint p, bool visible = true) { timeline.samples.push_back({t, p, visible, 0}); }
    void click(qint64 t, QPoint p, qint64 holdMs = 80)
    {
        move(t, p);
        timeline.buttons.push_back({t, Qt::LeftButton, true, p, false});
        timeline.buttons.push_back({t + holdMs, Qt::LeftButton, false, p, false});
    }
    void sort()
    {
        std::stable_sort(timeline.samples.begin(), timeline.samples.end(),
                         [](const CursorSample& a, const CursorSample& b) { return a.tMs < b.tMs; });
        std::stable_sort(timeline.buttons.begin(), timeline.buttons.end(),
                         [](const CursorButtonEvent& a, const CursorButtonEvent& b) { return a.tMs < b.tMs; });
    }
    std::vector<ZoomSegment> generate(qint64 durationMs = 60000)
    {
        sort();
        return generateAutoZoom(timeline, {kBounds, durationMs, 2.0});
    }
};

bool inside(const QRectF& outer, const QRectF& inner)
{
    constexpr double kEps = 1e-6;
    return inner.left() >= outer.left() - kEps && inner.top() >= outer.top() - kEps
        && inner.right() <= outer.right() + kEps && inner.bottom() <= outer.bottom() + kEps;
}

ZoomTrack trackOf(std::vector<ZoomSegment> segments)
{
    ZoomTrack track;
    track.setSegments(std::move(segments));
    return track;
}

} // namespace

class TestZoomTrack : public QObject
{
    Q_OBJECT

private slots:
    void repeatedClicksInOneArea();
    void fastMovesAcrossAreas();
    void cornerClicksStayInFrame();
    void dragKeepsItsPathVisible();
    void longReadingStaysWide();
    void hiddenAndOffFrameActivityIgnored();
    void nearbyGroupsDoNotPump();
    void viewportIsStableAndBounded();
    void reducedMotionAndOff();
    void segmentsNormalizeAndRoundTrip();
    void demoContactSheet();
};

void TestZoomTrack::repeatedClicksInOneArea()
{
    Script s;
    for (int i = 0; i < 5; ++i) s.click(5000 + i * 600, QPoint(400 + i * 10, 300));
    const auto segments = s.generate();
    QCOMPARE(int(segments.size()), 1);
    const ZoomSegment& z = segments.front();
    QVERIFY(z.startMs < 5000 && z.endMs > 5000 + 4 * 600);
    QCOMPARE(z.scale, 2.0); // the cap: a small area is not blown up further
    QVERIFY(!z.manual);
    const ZoomTrack track = trackOf(segments);
    for (int i = 0; i < 5; ++i) {
        // Every click is on screen, fully zoomed, when it happens.
        const QRectF view = track.viewportAt(5000 + i * 600, kBounds);
        QCOMPARE(view.width(), kBounds.width() / 2);
        QVERIFY(view.contains(QPointF(400 + i * 10, 300)));
    }
}

void TestZoomTrack::fastMovesAcrossAreas()
{
    Script s;
    s.click(3000, QPoint(200, 200));
    s.click(3400, QPoint(1700, 900)); // the far corner, right after
    const auto segments = s.generate();
    QCOMPARE(int(segments.size()), 2);
    const ZoomTrack track = trackOf(segments);
    // Each click visible at its time; the camera pans in between, inside the picture.
    QVERIFY(track.viewportAt(3000, kBounds).contains(QPointF(200, 200)));
    QVERIFY(segments[1].startMs >= segments[0].endMs);
    QVERIFY(track.viewportAt(segments[1].startMs, kBounds).contains(QPointF(1700, 900)));
    for (qint64 t = 0; t < 10000; t += 7) QVERIFY(inside(kBounds, track.viewportAt(t, kBounds)));
}

void TestZoomTrack::cornerClicksStayInFrame()
{
    Script s;
    s.click(2000, QPoint(5, 5));
    s.click(9000, QPoint(1915, 1075));
    const ZoomTrack track = trackOf(s.generate());
    QCOMPARE(int(track.segments().size()), 2);
    const QRectF topLeft = track.viewportAt(2000, kBounds);
    QCOMPARE(topLeft.topLeft(), QPointF(0, 0)); // clamped to the corner, not centred off-picture
    QVERIFY(topLeft.contains(QPointF(5, 5)));
    const QRectF bottomRight = track.viewportAt(9000, kBounds);
    QCOMPARE(bottomRight.bottomRight(), kBounds.bottomRight());
}

void TestZoomTrack::dragKeepsItsPathVisible()
{
    Script s;
    // Press at (300,300), drag to (700,500) over a second, release.
    s.timeline.buttons.push_back({4000, Qt::LeftButton, true, QPoint(300, 300), false});
    for (int i = 0; i <= 60; ++i) s.move(4000 + i * 16, QPoint(300 + i * 400 / 60, 300 + i * 200 / 60));
    s.timeline.buttons.push_back({4000 + 60 * 16, Qt::LeftButton, false, QPoint(700, 500), false});
    const ZoomTrack track = trackOf(s.generate());
    QCOMPARE(int(track.segments().size()), 1);
    for (int i = 0; i <= 60; ++i) {
        const qint64 t = 4000 + i * 16;
        QVERIFY(track.viewportAt(t, kBounds).contains(QPointF(300 + i * 400 / 60, 300 + i * 200 / 60)));
    }
    // A drag across the whole screen is not worth zooming: the view stays wide.
    Script wide;
    wide.timeline.buttons.push_back({4000, Qt::LeftButton, true, QPoint(50, 50), false});
    wide.move(4000, QPoint(50, 50));
    wide.move(4500, QPoint(1850, 1000));
    wide.timeline.buttons.push_back({4600, Qt::LeftButton, false, QPoint(1850, 1000), false});
    QVERIFY(wide.generate().empty());
}

void TestZoomTrack::longReadingStaysWide()
{
    Script s;
    // Thirty seconds of moving the pointer while reading, no clicks.
    for (int i = 0; i < 1800; ++i) s.move(i * 16, QPoint(800 + (i % 50), 400 + (i % 30)));
    QVERIFY(s.generate().empty());
    // And without any pointer data there is nothing to invent.
    QVERIFY(generateAutoZoom(CursorTimeline{}, {kBounds, 1000, 2.0}).empty());
}

void TestZoomTrack::hiddenAndOffFrameActivityIgnored()
{
    Script s;
    s.move(0, QPoint(100, 100));
    s.move(2000, QPoint(500, 500), false); // pointer hidden (a video player, a game)
    s.timeline.buttons.push_back({2100, Qt::LeftButton, true, QPoint(500, 500), false});
    s.timeline.buttons.push_back({2150, Qt::LeftButton, false, QPoint(500, 500), false});
    s.move(5000, QPoint(2500, 300)); // on another screen
    s.timeline.buttons.push_back({5100, Qt::LeftButton, true, QPoint(2500, 300), false});
    s.timeline.buttons.push_back({5150, Qt::LeftButton, false, QPoint(2500, 300), false});
    // A release SnapTray inserted at a pause is not a click.
    s.timeline.buttons.push_back({8000, Qt::LeftButton, false, QPoint(900, 900), true});
    QVERIFY(s.generate().empty());
}

void TestZoomTrack::nearbyGroupsDoNotPump()
{
    Script s;
    // Two bursts in the same region, 2.5 s apart: one continuous zoom, not out-and-in.
    s.click(4000, QPoint(600, 400));
    s.click(6500, QPoint(680, 430));
    const auto segments = s.generate();
    QCOMPARE(int(segments.size()), 1);
    const ZoomTrack track = trackOf(segments);
    for (qint64 t = 4000; t <= 6500; t += 50) QCOMPARE(track.viewportAt(t, kBounds).width(), kBounds.width() / 2);
}

void TestZoomTrack::viewportIsStableAndBounded()
{
    ZoomSegment a{2000, 4000, 2.0, QPointF(500, 400)};
    ZoomSegment b{8000, 9000, 3.0, QPointF(1500, 700)};
    const ZoomTrack track = trackOf({a, b});
    double lastWidth = kBounds.width();
    for (qint64 t = 0; t <= 12000; t += 5) {
        const QRectF view = track.viewportAt(t, kBounds);
        QVERIFY(inside(kBounds, view));
        // One aspect ratio throughout: no stretching.
        QVERIFY(qAbs(view.width() / view.height() - kBounds.width() / kBounds.height()) < 1e-6);
        // Easing in: the view only narrows, no overshoot past the target.
        if (t > 2000 - ZoomTrack::kTransitionMs && t <= 2000) {
            QVERIFY(view.width() <= lastWidth + 1e-9);
            QVERIFY(view.width() >= kBounds.width() / 2 - 1e-9);
        }
        lastWidth = view.width();
    }
    // Holds do not move; far from segments the picture is whole.
    QCOMPARE(track.viewportAt(2500, kBounds), track.viewportAt(3900, kBounds));
    QCOMPARE(track.viewportAt(6000, kBounds), kBounds);
    QCOMPARE(track.viewportAt(0, kBounds), kBounds);
    // Same time, same view, whatever was asked before (seek, re-export).
    const QRectF once = track.viewportAt(1700, kBounds);
    track.viewportAt(9999, kBounds);
    QCOMPARE(track.viewportAt(1700, kBounds), once);
    // Inside a crop: the view stays within the crop.
    const QRectF crop(200, 100, 800, 450);
    for (qint64 t = 0; t <= 12000; t += 25) QVERIFY(inside(crop, track.viewportAt(t, crop)));
}

void TestZoomTrack::reducedMotionAndOff()
{
    ZoomTrack track = trackOf({{2000, 4000, 3.0, QPointF(500, 400)}});
    track.reduceMotion = true;
    QCOMPARE(track.viewportAt(3000, kBounds).width(), kBounds.width() / ZoomTrack::kReducedMotionMaxScale);
    QVERIFY(track.viewportAt(2000 - ZoomTrack::kTransitionMs - 1, kBounds) != kBounds); // slower ramp
    track.enabled = false;
    QVERIFY(!track.isActive());
    QCOMPARE(track.viewportAt(3000, kBounds), kBounds);
}

void TestZoomTrack::segmentsNormalizeAndRoundTrip()
{
    ZoomTrack track;
    track.setSegments({{5000, 7000, 9.0, QPointF(1, 2), true},
                       {1000, 6000, 1.5, QPointF(3, 4)},
                       {6100, 6300, 2.0, QPointF(5, 6)}}); // too short once pushed after the previous
    QCOMPARE(int(track.segments().size()), 2);
    QCOMPARE(track.segments()[0].startMs, qint64(1000));
    QCOMPARE(track.segments()[1].startMs, qint64(6000)); // pushed after the first
    QCOMPARE(track.segments()[1].scale, ZoomTrack::kMaxScale);
    QCOMPARE(track.segmentAt(6500), 1);
    QCOMPARE(track.segmentAt(500), -1);
    track.reduceMotion = true;
    QCOMPARE(ZoomTrack::fromJson(track.toJson()), track);
}

void TestZoomTrack::demoContactSheet()
{
    // For people, not assertions: renders what auto zoom does to a realistic
    // session so its framing can be judged by eye.
    const QString out = qEnvironmentVariable("SNAPTRAY_ZOOM_DEMO_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_ZOOM_DEMO_DIR to render the auto zoom contact sheet");

    // A settings-like screen: small text, fields, a button in the corner, a slider.
    QImage screen(1920, 1080, QImage::Format_RGB32);
    screen.fill(QColor(245, 246, 248));
    {
        QPainter p(&screen);
        QFont font;
        font.setPixelSize(14);
        p.setFont(font);
        p.fillRect(0, 0, 1920, 48, QColor(40, 44, 52));
        p.setPen(Qt::white);
        p.drawText(20, 30, QStringLiteral("Preferences — Account"));
        p.setPen(QColor(60, 60, 60));
        for (int i = 0; i < 6; ++i) {
            p.drawText(240, 140 + i * 70, QStringLiteral("Field %1").arg(i + 1));
            p.drawRect(360, 120 + i * 70, 420, 30);
        }
        p.fillRect(1700, 980, 180, 50, QColor(30, 110, 230));
        p.setPen(Qt::white);
        p.drawText(1760, 1010, QStringLiteral("Save"));
        p.setPen(QColor(60, 60, 60));
        p.drawLine(1100, 600, 1700, 600);
        p.drawText(1100, 580, QStringLiteral("Volume"));
    }

    Script s;
    s.timeline.info.shapesAvailable = false;
    s.move(0, QPoint(900, 500));
    s.click(3000, QPoint(400, 135));   // field 1
    s.click(5200, QPoint(420, 205));   // field 2
    s.click(7400, QPoint(450, 275));   // field 3
    // A slider drag.
    s.timeline.buttons.push_back({12000, Qt::LeftButton, true, QPoint(1200, 600), false});
    for (int i = 0; i <= 45; ++i) s.move(12000 + i * 16, QPoint(1200 + i * 6, 600));
    s.timeline.buttons.push_back({12720, Qt::LeftButton, false, QPoint(1470, 600), false});
    s.click(13600, QPoint(1790, 1005)); // Save, bottom-right corner
    const auto segments = s.generate(16000);
    const ZoomTrack track = trackOf(segments);
    for (const auto& z : segments) {
        qInfo("zoom %lld-%lld ms, %.2fx at (%.0f, %.0f)", z.startMs, z.endMs, z.scale, z.focus.x(), z.focus.y());
    }

    CursorEffectSettings effects;
    effects.clickRipple = true;
    CursorEffectRenderer renderer(s.timeline, effects);
    const qint64 times[] = {1000, 2500, 3050, 5250, 7450, 9500, 11000, 12300, 12700, 13650, 15000, 15900};
    constexpr int kCols = 4;
    const QSize thumb(480, 270);
    QImage sheet(thumb.width() * kCols, (thumb.height() + 20) * 3, QImage::Format_RGB32);
    sheet.fill(Qt::black);
    QPainter sheetPainter(&sheet);
    sheetPainter.setPen(Qt::white);
    for (int i = 0; i < 12; ++i) {
        const QRectF view = track.viewportAt(times[i], kBounds);
        QImage frame(1920, 1080, QImage::Format_RGB32);
        QPainter p(&frame);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRectF(frame.rect()), screen, view);
        p.end();
        renderer.paint(frame, times[i], {view, QSize(1920, 1080), frame.size()});
        const QPoint at((i % kCols) * thumb.width(), (i / kCols) * (thumb.height() + 20));
        sheetPainter.drawImage(QRect(at + QPoint(0, 20), thumb), frame);
        sheetPainter.drawText(at + QPoint(6, 15), QStringLiteral("%1 ms  %2x").arg(times[i]).arg(1920.0 / view.width(), 0, 'f', 2));
    }
    sheetPainter.end();
    QVERIFY(sheet.save(QDir(out).filePath(QStringLiteral("auto-zoom-demo.png"))));
}

QTEST_MAIN(TestZoomTrack)
#include "tst_ZoomTrack.moc"
