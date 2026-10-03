#include <QtTest/QtTest>

#include "recording/WindowTimelineRecorder.h"

#include <functional>
#include <vector>

using SnapTray::WindowFrameMapping;
using SnapTray::WindowTimeline;
using SnapTray::WindowTimelineRecorder;

namespace {

// 2x screen at the logical origin: logical (x, y, w, h) -> video (2x, 2y, 2w, 2h).
const WindowFrameMapping kMapping{QRect(0, 0, 1000, 500), QRect(), 2.0, QRect(0, 0, 2000, 1000)};

DetectedElement element(quint32 id, const QRect& bounds, ElementType type = ElementType::Window, int layer = 0,
                        const char* app = "App")
{
    DetectedElement e;
    e.bounds = bounds;
    e.windowTitle = QStringLiteral("Secret title - must never be stored");
    e.ownerApp = QString::fromLatin1(app);
    e.windowLayer = layer;
    e.windowId = id;
    e.elementType = type;
    e.ownerPid = 100 + int(id);
    return e;
}

struct Fakes {
    std::vector<DetectedElement> elements;
    qint64 nowMs = 0;
    int enumerations = 0;
    WindowTimelineRecorder::Enumerator enumerator()
    {
        return [this]() { ++enumerations; return elements; };
    }
    WindowTimelineRecorder::Clock clock()
    {
        return [this]() { return nowMs; };
    }
};

} // namespace

class tst_WindowTimelineRecorder : public QObject
{
    Q_OBJECT

private slots:
    void sampleMapsFiltersAndOrders();
    void startSamplesImmediatelyThenOnInterval();
    void pauseStopsSamplingAndResumeContinues();
    void stopKeepsTimeline();
    void unchangedSamplesCompact();
};

void tst_WindowTimelineRecorder::sampleMapsFiltersAndOrders()
{
    Fakes fakes;
    fakes.elements = {
        element(1, QRect(0, 0, 500, 500), ElementType::Window, 0, "Back"),
        element(2, QRect(100, 100, 200, 100), ElementType::PopupMenu, 5, "Menu"),   // filtered out
        element(3, QRect(200, 200, 100, 100), ElementType::Dialog, 2, "Dialog"),   // higher layer: topmost
        element(4, QRect(900, 0, 300, 300), ElementType::Window, 0, "Edge"),       // clipped to the screen
        element(5, QRect(2000, 0, 100, 100), ElementType::Window, 0, "Elsewhere"), // off screen: dropped
    };
    fakes.nowMs = 1234;
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    recorder.sampleNow();

    const WindowTimeline timeline = recorder.timeline();
    QCOMPARE(timeline.frameSize(), QSize(2000, 1000));
    QCOMPARE(timeline.entries().size(), size_t(1));
    const auto& entry = timeline.entries().front();
    QCOMPARE(entry.tMs, qint64(1234));
    QCOMPARE(entry.windows.size(), size_t(3));
    QCOMPARE(entry.windows[0].windowId, quint32(3)); // layer 2 first
    QCOMPARE(entry.windows[0].z, 0);
    QCOMPARE(entry.windows[0].rect, QRect(400, 400, 200, 200));
    QCOMPARE(entry.windows[0].type, ElementType::Dialog);
    QCOMPARE(entry.windows[1].windowId, quint32(1)); // then enumeration order
    QCOMPARE(entry.windows[1].z, 1);
    QCOMPARE(entry.windows[2].windowId, quint32(4));
    QCOMPARE(entry.windows[2].rect, QRect(1800, 0, 200, 600));
    QCOMPARE(entry.windows[1].ownerApp, QStringLiteral("Back"));
    QVERIFY(!timeline.toJson().contains("Secret title"));
}

void tst_WindowTimelineRecorder::startSamplesImmediatelyThenOnInterval()
{
    Fakes fakes;
    fakes.elements = {element(1, QRect(0, 0, 100, 100))};
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    QVERIFY(!recorder.isRunning());
    recorder.start();
    QVERIFY(recorder.isRunning());
    QCOMPARE(fakes.enumerations, 1);
    // The timer fires on the GUI thread's event loop.
    QTRY_VERIFY_WITH_TIMEOUT(fakes.enumerations >= 3, WindowTimelineRecorder::kSampleIntervalMs * 4);
    recorder.stop();
    QVERIFY(!recorder.isRunning());
}

void tst_WindowTimelineRecorder::pauseStopsSamplingAndResumeContinues()
{
    Fakes fakes;
    fakes.elements = {element(1, QRect(0, 0, 100, 100))};
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    recorder.start();
    recorder.pause();
    QVERIFY(!recorder.isRunning());
    const int before = fakes.enumerations;
    QTest::qWait(WindowTimelineRecorder::kSampleIntervalMs * 2);
    QCOMPARE(fakes.enumerations, before);

    fakes.nowMs = 5000;                       // media time when the recording resumes
    fakes.elements = {element(2, QRect(0, 0, 100, 100))};
    recorder.resume();
    QVERIFY(recorder.isRunning());
    QCOMPARE(fakes.enumerations, before + 1); // immediate sample on resume
    const WindowTimeline timeline = recorder.timeline();
    QCOMPARE(timeline.entries().back().tMs, qint64(5000));
    QCOMPARE(timeline.entries().back().windows.front().windowId, quint32(2));
    recorder.stop();
}

void tst_WindowTimelineRecorder::stopKeepsTimeline()
{
    Fakes fakes;
    fakes.elements = {element(1, QRect(0, 0, 100, 100))};
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    recorder.start();
    recorder.stop();
    QCOMPARE(recorder.timeline().entries().size(), size_t(1));
    const int before = fakes.enumerations;
    QTest::qWait(WindowTimelineRecorder::kSampleIntervalMs * 2);
    QCOMPARE(fakes.enumerations, before);
    recorder.sampleNow(); // explicit samples after stop are ignored
    QCOMPARE(recorder.timeline().entries().size(), size_t(1));
}

void tst_WindowTimelineRecorder::unchangedSamplesCompact()
{
    Fakes fakes;
    fakes.elements = {element(1, QRect(0, 0, 100, 100))};
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    recorder.start();
    fakes.nowMs = 250;
    recorder.sampleNow();
    fakes.nowMs = 500;
    recorder.sampleNow();
    fakes.elements = {element(1, QRect(10, 0, 100, 100))};
    fakes.nowMs = 750;
    recorder.sampleNow();
    recorder.stop();
    const WindowTimeline timeline = recorder.timeline();
    QCOMPARE(timeline.entries().size(), size_t(2));
    QCOMPARE(timeline.entries()[1].tMs, qint64(750));
}

QTEST_MAIN(tst_WindowTimelineRecorder)
#include "tst_WindowTimelineRecorder.moc"
