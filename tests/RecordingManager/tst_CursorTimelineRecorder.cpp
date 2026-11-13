#include <QtTest>

#include <QElapsedTimer>
#include <QTemporaryDir>

#include <functional>
#include <QThread>

#include "recording/CursorTimelineRecorder.h"
#include "recording/CursorEffects.h"
#include "recording/WindowsCursorSupport.h"

using namespace SnapTray;

#if defined(SNAPTRAY_TEST_HAS_XTEST)
// x11_pointer_helper.cpp: drives the isolated X display through XTest.
void snaptrayTestX11Pointer(const char* action, int a, int b);
#endif

namespace {

// 1:1 screen at the origin, recorded whole.
const WindowFrameMapping kMapping{QRect(0, 0, 800, 600), QRect(0, 0, 800, 600), 1.0, QRect(0, 0, 800, 600)};

QImage solid(int size, QColor color)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

struct ProbeState {
    CursorProbe::Capabilities caps{true, true};
    CursorProbe::State state{QPoint(10, 10), true, 1};
    std::optional<CursorProbe::Shape> shape = CursorProbe::Shape{solid(16, Qt::black), QPoint(0, 0)};
    int shapeReads = 0;
    std::function<void()> onPoll; // events the next poll delivers
};

class FakeProbe : public CursorProbe
{
public:
    explicit FakeProbe(ProbeState* s) : m_s(s) {}
    Capabilities capabilities() const override { return m_s->caps; }
    std::optional<State> poll() override
    {
        // Events queued on the probe's connection are delivered when it polls.
        if (m_s->onPoll) {
            auto deliver = std::move(m_s->onPoll);
            m_s->onPoll = nullptr;
            deliver();
        }
        return m_s->state;
    }
    std::optional<Shape> currentShape() override
    {
        ++m_s->shapeReads;
        return m_s->caps.shapes ? m_s->shape : std::nullopt;
    }
    QString name() const override { return QStringLiteral("fake"); }
    void press(Qt::MouseButton b, bool down, qint64 ageMs = 0,
               std::optional<QPoint> physicalPosition = std::nullopt)
    {
        emit buttonChanged(b, down, ageMs, physicalPosition);
    }
    void loseEvents() { emit buttonEventsLost(); }

private:
    ProbeState* m_s;
};

struct Rig {
    QTemporaryDir dir;
    ProbeState probeState;
    FakeProbe* probe = nullptr;
    qint64 now = 0;
    std::unique_ptr<CursorTimelineRecorder> recorder;
    QString path;

    explicit Rig(WindowFrameMapping mapping = kMapping)
    {
        path = dir.filePath(QStringLiteral("rec.mp4.cursor.jsonl"));
        auto owned = std::make_unique<FakeProbe>(&probeState);
        probe = owned.get();
        recorder = std::make_unique<CursorTimelineRecorder>(std::move(owned), [this] { return now; }, mapping,
                                                            path, false);
    }
    CursorTimeline finish()
    {
        recorder->stop();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return CursorTimeline::parse(file.readAll()).value_or(CursorTimeline{});
    }
};

} // namespace

class TestCursorTimelineRecorder : public QObject
{
    Q_OBJECT

private slots:
    void startRecordsTheInitialPointer();
    void onlyChangesAreWritten();
    void smoothingPreservesStationaryGaps();
    void shapeChangesInPlaceAndIsDeduplicated();
    void hiddenPointer();
    void clickShorterThanAPollIsKept();
    void pauseReleasesAndIgnores();
    void stopReleasesHeldButtons();
    void withoutClickSupportNoButtons();
    void positionOnlyProbeUsesLogicalMapping();
    void unwritableSidecarDoesNotStart();
    void timerSamplesWhileRunning();
    void x11ProbeOnIsolatedDisplay();
    void x11RecorderKeepsQueuedClicks();
    void queuedClicksKeepTheirSourceTime();
    void x11QueuedClickKeepsItsDuration();
    void x11ClicksDuringAnotherPointerGrab();
    void queuedClicksKeepTheirNativePositions_data();
    void queuedClicksKeepTheirNativePositions();
    void lateClickTakesThePositionAtItsTime();
    void clicksFromAPauseAreDropped();
    void lostButtonEventsReleaseWhatLooksHeld();
    void lossesWhilePausedAreNotCounted();
};

void TestCursorTimelineRecorder::startRecordsTheInitialPointer()
{
    Rig rig;
    rig.now = 5;
    QVERIFY(rig.recorder->start());
    QVERIFY(rig.recorder->isRunning());
    const auto timeline = rig.finish();
    QVERIFY(timeline.finished);
    QVERIFY(timeline.info.shapesAvailable);
    QVERIFY(timeline.info.clicksAvailable);
    QCOMPARE(timeline.info.frameSize, QSize(800, 600));
    QCOMPARE(int(timeline.shapes.size()), 1);
    QVERIFY(!timeline.samples.empty());
    QCOMPARE(timeline.samples.front().tMs, qint64(5));
    QCOMPARE(timeline.samples.front().position, QPoint(10, 10));
    QCOMPARE(timeline.samples.front().shapeId, 1u);
}

void TestCursorTimelineRecorder::onlyChangesAreWritten()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    for (int i = 0; i < 10; ++i) {
        rig.now += 16;
        rig.recorder->sampleNow();
    }
    rig.probeState.state.position = QPoint(11, 10);
    rig.now += 16;
    rig.recorder->sampleNow();
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.samples.size()), 2);
    QCOMPARE(timeline.samples[1].tMs, qint64(176));
    // The shape is read only when its serial changes, not every poll.
    QCOMPARE(rig.probeState.shapeReads, 1);
}

void TestCursorTimelineRecorder::smoothingPreservesStationaryGaps()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    for (rig.now = 16; rig.now < 10000; rig.now += 16)
        rig.recorder->sampleNow();
    rig.now = 10000;
    rig.probeState.state.position = QPoint(110, 10);
    rig.recorder->sampleNow();
    const auto timeline = rig.finish();
    QCOMPARE(timeline.samples.size(), size_t(2));
    for (auto level : {CursorEffectSettings::Smoothing::Low,
                       CursorEffectSettings::Smoothing::Medium,
                       CursorEffectSettings::Smoothing::High}) {
        CursorEffectSettings settings;
        settings.smoothing = level;
        CursorEffectRenderer renderer(timeline, settings);
        // Change-only samples must not imply movement throughout the idle gap.
        for (qint64 ms : {1000, 5000, 9000})
            QCOMPARE(*renderer.pointerPosition(ms), QPointF(10, 10));
        QVERIFY(renderer.pointerPosition(10500)->x() > 109.0);
        renderer.setTimeline(TimelineMap({{2000, 11000, 1.0}}));
        QCOMPARE(*renderer.pointerPosition(5000), QPointF(10, 10));
    }
}

void TestCursorTimelineRecorder::shapeChangesInPlaceAndIsDeduplicated()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    // I-beam over text without moving.
    rig.probeState.state.shapeSerial = 2;
    rig.probeState.shape = CursorProbe::Shape{solid(16, Qt::blue), QPoint(8, 8)};
    rig.now = 10;
    rig.recorder->sampleNow();
    // Back to the arrow: the same image is stored once.
    rig.probeState.state.shapeSerial = 3;
    rig.probeState.shape = CursorProbe::Shape{solid(16, Qt::black), QPoint(0, 0)};
    rig.now = 20;
    rig.recorder->sampleNow();
    // Same image, other hotspot (a resize cursor): a distinct shape.
    rig.probeState.state.shapeSerial = 4;
    rig.probeState.shape = CursorProbe::Shape{solid(16, Qt::black), QPoint(3, 3)};
    rig.now = 30;
    rig.recorder->sampleNow();
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.shapes.size()), 3);
    QCOMPARE(int(timeline.samples.size()), 4);
    QCOMPARE(timeline.samples[1].shapeId, 2u);
    QCOMPARE(timeline.samples[1].position, QPoint(10, 10));
    QCOMPARE(timeline.samples[2].shapeId, 1u);
    QCOMPARE(timeline.samples[3].shapeId, 3u);
    QCOMPARE(timeline.shape(2)->hotspot, QPoint(8, 8));
}

void TestCursorTimelineRecorder::hiddenPointer()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    rig.probeState.state.shapeSerial = 2;
    rig.probeState.shape = CursorProbe::Shape{solid(16, Qt::transparent), QPoint()};
    rig.now = 10;
    rig.recorder->sampleNow();
    rig.probeState.state.shapeSerial = 3;
    rig.probeState.shape = CursorProbe::Shape{solid(16, Qt::black), QPoint(0, 0)};
    rig.now = 20;
    rig.recorder->sampleNow();
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.samples.size()), 3);
    QVERIFY(!timeline.samples[1].visible);
    QVERIFY(timeline.samples[2].visible);
    QCOMPARE(timeline.samples[2].shapeId, 1u);
    QCOMPARE(int(timeline.shapes.size()), 1); // the empty image is not stored
}

void TestCursorTimelineRecorder::clickShorterThanAPollIsKept()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    // Between two polls: moved and clicked twice (a double-click).
    rig.probeState.state.position = QPoint(300, 200);
    rig.now = 4;
    rig.probe->press(Qt::LeftButton, true);
    rig.now = 6;
    rig.probe->press(Qt::LeftButton, false);
    rig.now = 9;
    rig.probe->press(Qt::LeftButton, true);
    rig.now = 11;
    rig.probe->press(Qt::LeftButton, false);
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.buttons.size()), 4);
    QCOMPARE(timeline.buttons[0].tMs, qint64(4));
    QCOMPARE(timeline.buttons[0].position, QPoint(300, 200)); // where it happened
    QCOMPARE(timeline.buttons[3].tMs, qint64(11));
    QVERIFY(!timeline.buttons[3].down);
    QVERIFY(!timeline.buttons[3].synthetic);
}

void TestCursorTimelineRecorder::pauseReleasesAndIgnores()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    rig.now = 100;
    rig.probe->press(Qt::LeftButton, true); // a drag in progress
    rig.now = 150;
    rig.recorder->pause();
    QVERIFY(!rig.recorder->isRunning());
    // Nothing is recorded while paused.
    rig.probeState.state.position = QPoint(500, 500);
    rig.probe->press(Qt::LeftButton, false);
    rig.probe->press(Qt::RightButton, true);
    rig.recorder->sampleNow();
    // Media time does not advance during a pause.
    rig.recorder->resume();
    rig.now = 151;
    // The release of a press made while paused is not a recorded release.
    rig.probe->press(Qt::RightButton, false);
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.buttons.size()), 2);
    QVERIFY(timeline.buttons[0].down);
    QVERIFY(!timeline.buttons[1].down);
    QVERIFY(timeline.buttons[1].synthetic);
    QCOMPARE(timeline.buttons[1].tMs, qint64(150));
    QCOMPARE(timeline.buttonsDownAt(10000), Qt::MouseButtons());
    // Resume rebuilds the state: a sample at resume with the new position.
    QCOMPARE(timeline.samples.back().position, QPoint(500, 500));
    QCOMPARE(timeline.samples.back().tMs, qint64(150));
}

void TestCursorTimelineRecorder::stopReleasesHeldButtons()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    rig.now = 40;
    rig.probe->press(Qt::LeftButton, true);
    rig.probe->press(Qt::MiddleButton, true);
    rig.now = 50;
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.buttons.size()), 4);
    QCOMPARE(timeline.buttonsDownAt(50), Qt::MouseButtons());
    QVERIFY(timeline.buttons[2].synthetic && timeline.buttons[3].synthetic);
    // After stop nothing more is written.
    rig.probe->press(Qt::LeftButton, true);
    QCOMPARE(int(rig.finish().buttons.size()), 4);
}

void TestCursorTimelineRecorder::withoutClickSupportNoButtons()
{
    Rig rig;
    rig.probeState.caps = {true, false};
    rig.recorder = std::make_unique<CursorTimelineRecorder>(
        std::make_unique<FakeProbe>(&rig.probeState), [&rig] { return rig.now; }, kMapping, rig.path, true);
    QVERIFY(rig.recorder->start());
    const auto timeline = rig.finish();
    QVERIFY(!timeline.info.clicksAvailable);
    QVERIFY(timeline.info.cursorInVideo);
    QVERIFY(timeline.buttons.empty());
}

void TestCursorTimelineRecorder::positionOnlyProbeUsesLogicalMapping()
{
    // A 2x screen: logical Qt coordinates become video pixels.
    Rig rig(WindowFrameMapping{QRect(0, 0, 1440, 900), QRect(), 2.0, QRect(0, 0, 2880, 1800)});
    rig.probeState.caps = {};
    rig.probeState.state = {QPoint(100, 50), false, 0};
    rig.recorder = std::make_unique<CursorTimelineRecorder>(
        std::make_unique<FakeProbe>(&rig.probeState), [&rig] { return rig.now; },
        WindowFrameMapping{QRect(0, 0, 1440, 900), QRect(), 2.0, QRect(0, 0, 2880, 1800)}, rig.path, false);
    QVERIFY(rig.recorder->start());
    const auto timeline = rig.finish();
    QVERIFY(!timeline.info.shapesAvailable);
    QCOMPARE(timeline.samples.front().position, QPoint(200, 100));
    QCOMPARE(timeline.samples.front().shapeId, 0u);
    QVERIFY(timeline.samples.front().visible);
    QCOMPARE(rig.probeState.shapeReads, 0);
}

void TestCursorTimelineRecorder::unwritableSidecarDoesNotStart()
{
    ProbeState state;
    CursorTimelineRecorder recorder(std::make_unique<FakeProbe>(&state), [] { return 0; }, kMapping,
                                    QStringLiteral("/nonexistent-dir/x.cursor.jsonl"), false);
    QVERIFY(!recorder.start());
    QVERIFY(!recorder.isRunning());
    QVERIFY(recorder.failed());
}

void TestCursorTimelineRecorder::timerSamplesWhileRunning()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    rig.probeState.state.position = QPoint(42, 42);
    rig.now = 30;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        QFile file(rig.path);
        // flushed every second
        return file.open(QIODevice::ReadOnly) && file.readAll().contains("\"x\":42");
    }(), 3000);
    rig.finish();
}

void TestCursorTimelineRecorder::x11ProbeOnIsolatedDisplay()
{
#if defined(SNAPTRAY_TEST_HAS_XTEST)
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != QLatin1String("1")
        || QGuiApplication::platformName() != QLatin1String("xcb")) {
        QSKIP("Moves and clicks the pointer: runs only on an isolated X display (ctest provides one).");
    }
    auto probe = createCursorProbe();
    QCOMPARE(probe->name(), QStringLiteral("x11"));
    QVERIFY(probe->capabilities().shapes);
    QVERIFY(probe->capabilities().clicks);

    snaptrayTestX11Pointer("move", 123, 45);
    QTRY_COMPARE(probe->poll()->position, QPoint(123, 45));
    QVERIFY(probe->poll()->physical);
    const quint64 serialBefore = probe->poll()->shapeSerial;
    const auto shapeBefore = probe->currentShape();
    QVERIFY(shapeBefore && !shapeBefore->image.isNull());

    // A different pointer shape without moving.
    snaptrayTestX11Pointer("shape", 152 /* XC_xterm */, 0);
    QTRY_VERIFY(probe->poll()->shapeSerial != serialBefore);
    const auto shapeAfter = probe->currentShape();
    QVERIFY(shapeAfter);
    QVERIFY(shapeAfter->image != shapeBefore->image || shapeAfter->hotspot != shapeBefore->hotspot);

    QList<QPair<Qt::MouseButton, bool>> events;
    connect(probe.get(), &CursorProbe::buttonChanged, this,
            [&events](Qt::MouseButton b, bool down) { events.append({b, down}); });
    // A fast click with no poll in between, then a right click.
    snaptrayTestX11Pointer("button", 1, 1);
    snaptrayTestX11Pointer("button", 1, 0);
    snaptrayTestX11Pointer("button", 3, 1);
    snaptrayTestX11Pointer("button", 3, 0);
    // Wheel steps are not clicks.
    snaptrayTestX11Pointer("button", 4, 1);
    snaptrayTestX11Pointer("button", 4, 0);
    QTRY_COMPARE(events.size(), 4);
    QCOMPARE(events[0], qMakePair(Qt::LeftButton, true));
    QCOMPARE(events[1], qMakePair(Qt::LeftButton, false));
    QCOMPARE(events[2], qMakePair(Qt::RightButton, true));

    // Left-handed mapping (3 2 1): the physical first button acts as button
    // 3, so it is recorded as Right, as applications receive it.
    snaptrayTestX11Pointer("swap", 1, 0);
    probe->poll(); // let the probe see the mapping change
    events.clear();
    snaptrayTestX11Pointer("button", 1, 1);
    snaptrayTestX11Pointer("button", 1, 0);
    QTRY_COMPARE(events.size(), 2);
    QCOMPARE(events[0].first, Qt::RightButton);
    snaptrayTestX11Pointer("swap", 0, 0);

    // Cost per poll (one X round trip): recorded for the PR, bounded loosely.
    constexpr int kPolls = 600; // ten seconds of polling at 60 Hz
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < kPolls; ++i) {
        snaptrayTestX11Pointer("move", 100 + i % 300, 100);
        QVERIFY(probe->poll());
    }
    const double usPerPoll = timer.nsecsElapsed() / 1000.0 / kPolls;
    qInfo("X11 pointer poll with a move: %.0f us", usPerPoll);
    QVERIFY(usPerPoll < 5000);
#else
    QSKIP("Built without XTest");
#endif
}

void TestCursorTimelineRecorder::x11RecorderKeepsQueuedClicks()
{
#if defined(SNAPTRAY_TEST_HAS_XTEST)
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != QLatin1String("1")
        || QGuiApplication::platformName() != QLatin1String("xcb")) {
        QSKIP("Clicks the pointer: runs only on an isolated X display (ctest provides one).");
    }
    // PR #174 review: the real probe connected to the recorder. A press and
    // its release queued together used to lose the release, because the
    // press handler's position poll drained the queue re-entrantly.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("x11.cursor.jsonl"));
    qint64 now = 0;
    CursorTimelineRecorder recorder(createCursorProbe(), [&now] { return now; }, kMapping, path, false);
    QVERIFY(recorder.start());
    // The clicks come after the start, as with a real clock: a click's
    // delivery delay must not place it at or before the start (dropped).
    now += 1000;
    snaptrayTestX11Pointer("move", 50, 60);
    for (int i = 0; i < 2; ++i) {
        // Both events reach the socket before SnapTray reads either.
        snaptrayTestX11Pointer("button", 1, 1);
        snaptrayTestX11Pointer("button", 1, 0);
        QTest::qWait(150);
        now += 200;
    }
    recorder.stop();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto timeline = CursorTimeline::parse(file.readAll());
    QVERIFY(timeline);
    QCOMPARE(int(timeline->buttons.size()), 4); // two real clicks, nothing synthetic
    for (const auto& event : timeline->buttons) QVERIFY(!event.synthetic);
    QCOMPARE(timeline->buttons.back().down, false);
#else
    QSKIP("Built without XTest");
#endif
}

void TestCursorTimelineRecorder::queuedClicksKeepTheirSourceTime()
{
    // PR #174 review: events delivered late (a busy GUI thread) are stamped
    // when they happened, not when they arrived.
    Rig rig;
    rig.now = 100;
    QVERIFY(rig.recorder->start());
    rig.now = 1000;
    rig.probe->press(Qt::LeftButton, true, 400);
    rig.probe->press(Qt::LeftButton, false, 100);
    // Never out of order: a later-delivered event that claims to be older
    // than the previous one is placed right after it.
    rig.now = 1200;
    rig.probe->press(Qt::RightButton, true, 400);
    rig.probe->press(Qt::RightButton, false, 450);
    // From before the recording started: not part of it.
    rig.probe->press(Qt::MiddleButton, true, 5000);
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.buttons.size()), 4);
    QCOMPARE(timeline.buttons[0].tMs, qint64(600));
    QCOMPARE(timeline.buttons[1].tMs, qint64(900));
    QCOMPARE(timeline.buttons[2].tMs, qint64(900));
    QCOMPARE(timeline.buttons[3].tMs, qint64(900));
    for (const auto& event : timeline.buttons) QVERIFY(event.button != Qt::MiddleButton);
}

void TestCursorTimelineRecorder::x11QueuedClickKeepsItsDuration()
{
#if defined(SNAPTRAY_TEST_HAS_XTEST)
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != QLatin1String("1")
        || QGuiApplication::platformName() != QLatin1String("xcb")) {
        QSKIP("Clicks the pointer: runs only on an isolated X display (ctest provides one).");
    }
    // PR #174 review: a 300 ms press made while the GUI thread is blocked
    // must not collapse into an instant click when both events arrive.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("x11-held.cursor.jsonl"));
    QElapsedTimer clock;
    clock.start();
    CursorTimelineRecorder recorder(createCursorProbe(), [&clock] { return clock.elapsed(); }, kMapping, path,
                                    false);
    QVERIFY(recorder.start());
    snaptrayTestX11Pointer("move", 50, 60);
    QTest::qWait(50);
    snaptrayTestX11Pointer("button", 1, 1);
    QThread::msleep(300); // no event processing: both events queue up
    snaptrayTestX11Pointer("button", 1, 0);
    QTest::qWait(150);
    recorder.stop();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto timeline = CursorTimeline::parse(file.readAll());
    QVERIFY(timeline);
    QCOMPARE(int(timeline->buttons.size()), 2);
    const qint64 held = timeline->buttons[1].tMs - timeline->buttons[0].tMs;
    QVERIFY2(held >= 250 && held <= 400, qPrintable(QString::number(held)));
#else
    QSKIP("Built without XTest");
#endif
}

void TestCursorTimelineRecorder::x11ClicksDuringAnotherPointerGrab()
{
#if defined(SNAPTRAY_TEST_HAS_XTEST)
    if (qEnvironmentVariable("SNAPTRAY_TEST_ISOLATED_X11") != QLatin1String("1")
        || QGuiApplication::platformName() != QLatin1String("xcb")) {
        QSKIP("Clicks the pointer: runs only on an isolated X display (ctest provides one).");
    }
    // PR #174 review: with XI 2.0 the server filters raw button events for
    // clients that do not hold an active grab, so a click while another
    // application grabs the pointer was lost (a press without its release
    // looked held until stop). XI 2.1 delivers them regardless.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("x11-grab.cursor.jsonl"));
    qint64 now = 0;
    CursorTimelineRecorder recorder(createCursorProbe(), [&now] { return now; }, kMapping, path, false);
    QVERIFY(recorder.start());
    // The clicks come after the start, as with a real clock: a click's
    // delivery delay must not place it at or before the start (dropped).
    now += 1000;
    snaptrayTestX11Pointer("move", 70, 80);
    snaptrayTestX11Pointer("grab", 1, 0);
    snaptrayTestX11Pointer("button", 1, 1);
    QTest::qWait(50);
    now += 100;
    snaptrayTestX11Pointer("button", 1, 0);
    QTest::qWait(100);
    snaptrayTestX11Pointer("grab", 0, 0);
    recorder.stop();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto timeline = CursorTimeline::parse(file.readAll());
    QVERIFY(timeline);
    QCOMPARE(int(timeline->buttons.size()), 2);
    for (const auto& event : timeline->buttons) QVERIFY(!event.synthetic);
#else
    QSKIP("Built without XTest");
#endif
}

void TestCursorTimelineRecorder::queuedClicksKeepTheirNativePositions_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<int>("deliveryMs");
    QTest::newRow("100-percent-between-polls") << qreal(1.0) << 16;
    QTest::newRow("150-percent-between-polls") << qreal(1.5) << 16;
    QTest::newRow("200-percent-gui-stall") << qreal(2.0) << 1000;
}

void TestCursorTimelineRecorder::queuedClicksKeepTheirNativePositions()
{
    QFETCH(qreal, dpr);
    QFETCH(int, deliveryMs);
    // A secondary monitor left of the primary, with a cropped capture origin.
    // Native positions must be translated to video pixels, never scaled again.
    const QRect physicalScreen(-1920, 0, 1920, 1080);
    const QRect region(-1800, 100, 800, 600);
    const WindowFrameMapping mapping{QRect(-1920, 0, qRound(1920 / dpr), qRound(1080 / dpr)),
                                     physicalScreen, dpr, region};
    Rig rig(mapping);
    rig.probeState.state.position = region.topLeft() + QPoint(10, 10);
    QVERIFY(rig.recorder->start());

    // Both events happen after the last sample and before the next poll.
    // The pointer then moves again before the GUI drains the Windows queue.
    WindowsCursor::ButtonEventPump<8> pump;
    pump.push({Qt::LeftButton, true}, 8u, region.topLeft() + QPoint(400, 300));
    pump.push({Qt::LeftButton, false}, 10u, region.topLeft() + QPoint(420, 310));
    rig.probeState.state.position = region.topLeft() + QPoint(700, 500);
    rig.now = deliveryMs;
    rig.probeState.onPoll = [&] {
        pump.drain(quint32(deliveryMs), [&](const WindowsCursor::ButtonChange& change, qint64 ageMs,
                                          const QPoint& position) {
            rig.probe->press(change.button, change.down, ageMs, position);
        });
    };
    rig.recorder->sampleNow();

    const auto timeline = rig.finish();
    QCOMPARE(timeline.buttons.size(), size_t(2));
    QCOMPARE(timeline.buttons[0].tMs, qint64(8));
    QCOMPARE(timeline.buttons[0].position, QPoint(400, 300));
    QVERIFY(timeline.buttons[0].down);
    QCOMPARE(timeline.buttons[1].tMs, qint64(10));
    QCOMPARE(timeline.buttons[1].position, QPoint(420, 310));
    QVERIFY(!timeline.buttons[1].down);
    QVERIFY(!timeline.buttons[0].synthetic && !timeline.buttons[1].synthetic);
}

void TestCursorTimelineRecorder::lateClickTakesThePositionAtItsTime()
{
    // PR #174 review: a click delivered late (busy GUI thread) took the
    // pointer's position at delivery, possibly far from where it happened.
    Rig rig;
    rig.probeState.state.position = QPoint(10, 10);
    QVERIFY(rig.recorder->start());
    rig.now = 100;
    rig.probeState.state.position = QPoint(50, 50);
    rig.recorder->sampleNow();
    rig.now = 400;
    // Clicked at 150 (at 50,50), delivered at 400 after moving to 300,200.
    rig.probeState.state.position = QPoint(300, 200);
    rig.probe->press(Qt::LeftButton, true, 250);
    rig.probe->press(Qt::LeftButton, false, 0);
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.buttons.size()), 2);
    QCOMPARE(timeline.buttons[0].tMs, qint64(150));
    QCOMPARE(timeline.buttons[0].position, QPoint(50, 50));
    // On time: where the pointer is now.
    QCOMPARE(timeline.buttons[1].position, QPoint(300, 200));
}

void TestCursorTimelineRecorder::clicksFromAPauseAreDropped()
{
    // PR #174 review: button events made during a pause could still be
    // queued at resume; they were accepted and moved to the resume time,
    // recording a click that happened while paused (and a held button).
    Rig rig;
    QVERIFY(rig.recorder->start());
    rig.now = 100;
    rig.recorder->pause();
    rig.now = 500;
    // Queued during the pause, delivered by the first poll after resume.
    rig.probeState.onPoll = [&rig] { rig.probe->press(Qt::LeftButton, true, 200); };
    rig.recorder->resume();
    // A late event whose real time is in the pause is not counted either.
    rig.now = 520;
    rig.probe->press(Qt::RightButton, true, 100);
    // A real click after resume still is.
    rig.now = 600;
    rig.probe->press(Qt::MiddleButton, true, 0);
    rig.probe->press(Qt::MiddleButton, false, 0);
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.buttons.size()), 2);
    QCOMPARE(timeline.buttons[0].button, Qt::MiddleButton);
    QVERIFY(!timeline.buttons[0].synthetic && !timeline.buttons[1].synthetic);
}

void TestCursorTimelineRecorder::lostButtonEventsReleaseWhatLooksHeld()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    rig.now = 40;
    rig.probe->press(Qt::LeftButton, true);
    // The release was lost; the probe says so.
    rig.now = 60;
    rig.probe->loseEvents();
    QCOMPARE(rig.recorder->buttonEventLosses(), 1);
    // The next click is a click of its own, not the end of a long drag.
    rig.now = 500;
    rig.probe->press(Qt::LeftButton, true);
    rig.now = 520;
    rig.probe->press(Qt::LeftButton, false);
    const auto timeline = rig.finish();
    QCOMPARE(int(timeline.buttons.size()), 4);
    QVERIFY(timeline.buttons[0].down && !timeline.buttons[0].synthetic);
    QCOMPARE(timeline.buttons[0].tMs, qint64(40));
    QVERIFY(!timeline.buttons[1].down && timeline.buttons[1].synthetic);
    QCOMPARE(timeline.buttons[1].tMs, qint64(60));
    QVERIFY(timeline.buttons[2].down && !timeline.buttons[2].synthetic);
    QCOMPARE(timeline.buttons[2].tMs, qint64(500));
    QVERIFY(!timeline.buttons[3].down && !timeline.buttons[3].synthetic);
    QCOMPARE(timeline.buttons[3].tMs, qint64(520));
    QCOMPARE(timeline.buttonsDownAt(100), Qt::MouseButtons());
}

void TestCursorTimelineRecorder::lossesWhilePausedAreNotCounted()
{
    Rig rig;
    QVERIFY(rig.recorder->start());
    rig.now = 40;
    rig.recorder->pause();
    // Events from a pause are dropped anyway: losing some is not a loss.
    rig.probe->loseEvents();
    rig.recorder->resume();
    QCOMPARE(rig.recorder->buttonEventLosses(), 0);
    QVERIFY(rig.finish().buttons.empty());
}

QTEST_MAIN(TestCursorTimelineRecorder)
#include "tst_CursorTimelineRecorder.moc"
