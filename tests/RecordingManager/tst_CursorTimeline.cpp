#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "recording/CursorTimeline.h"
#include "recording/RecordingSession.h"
#include "recording/WindowTimeline.h"

using namespace SnapTray;

namespace {

QImage arrow(int size = 16, QColor color = Qt::black)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size / 2; ++x) image.setPixelColor(x, y, color);
    return image;
}

CursorTimelineInfo info()
{
    CursorTimelineInfo i;
    i.frameSize = QSize(1920, 1080);
    i.shapesAvailable = true;
    i.clicksAvailable = true;
    return i;
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestCursorTimeline : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void tornTailKeepsWhatCameBefore();
    void unusableFilesAreRejected_data();
    void unusableFilesAreRejected();
    void damagedShapeDegradesToUnknown();
    void sizeLimitStillClosesPresses();
    void shapeLimits();
    void queries();
    void videoPointMapping_data();
    void videoPointMapping();
    void sidecarFollowsTheRecording();
    void reportReplaysEvents();
    void growthStaysWithinTheLimit();
};

void TestCursorTimeline::roundTrip()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_r.mp4"));
    CursorTimelineWriter writer(CursorTimelineSidecar::pathFor(video));
    QVERIFY(writer.open(info()));
    QVERIFY(writer.addShape({1, QPoint(1, 1), arrow()}));
    QVERIFY(writer.addShape({2, QPoint(4, 8), arrow(16, Qt::red)}));
    writer.addSample({0, QPoint(10, 20), true, 1});
    writer.addSample({16, QPoint(-5, 2000), true, 2}); // off the frame: kept as is
    writer.addSample({40, QPoint(-5, 2000), false, 2});
    writer.addButton({20, Qt::LeftButton, true, QPoint(11, 21), false});
    writer.addButton({25, Qt::LeftButton, false, QPoint(11, 21), false});
    writer.addButton({30, Qt::RightButton, true, QPoint(12, 22), false});
    writer.addButton({50, Qt::RightButton, false, QPoint(12, 22), true});
    QVERIFY(writer.finish());
    QVERIFY(writer.finish()); // idempotent

    const auto timeline = CursorTimelineSidecar::read(video);
    QVERIFY(timeline);
    QCOMPARE(timeline->info.frameSize, QSize(1920, 1080));
    QVERIFY(timeline->info.shapesAvailable);
    QVERIFY(timeline->info.clicksAvailable);
    QVERIFY(!timeline->info.cursorInVideo);
    QVERIFY(timeline->finished);
    QVERIFY(!timeline->truncated);
    QCOMPARE(int(timeline->shapes.size()), 2);
    QCOMPARE(timeline->shape(2)->hotspot, QPoint(4, 8));
    QCOMPARE(timeline->shape(2)->image.pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(int(timeline->samples.size()), 3);
    QCOMPARE(timeline->samples[1].position, QPoint(-5, 2000));
    QVERIFY(!timeline->samples[2].visible);
    QCOMPARE(int(timeline->buttons.size()), 4);
    QCOMPARE(timeline->buttons[2].button, Qt::RightButton);
    QVERIFY(timeline->buttons[3].synthetic);
}

void TestCursorTimeline::tornTailKeepsWhatCameBefore()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("torn.cursor.jsonl"));
    {
        CursorTimelineWriter writer(path);
        QVERIFY(writer.open(info()));
        writer.addSample({0, QPoint(1, 1), true, 0});
        writer.addSample({16, QPoint(2, 2), true, 0});
        writer.flush();
        // A crash: no end record, and a half-written line.
        QFile file(path);
        QVERIFY(file.open(QIODevice::Append));
        file.write(R"({"type":"move","t":32,"x":3,)");
    }
    const auto timeline = CursorTimeline::parse(readAll(path));
    QVERIFY(timeline);
    QVERIFY(!timeline->finished);
    QCOMPARE(int(timeline->samples.size()), 2);
    QCOMPARE(timeline->samples.back().position, QPoint(2, 2));
}

void TestCursorTimeline::unusableFilesAreRejected_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("not json") << QByteArray("garbage\n");
    QTest::newRow("other format") << QByteArray(R"({"type":"windows","version":1,"frame":[10,10]})" "\n");
    QTest::newRow("newer version") << QByteArray(R"({"type":"cursor-timeline","version":2,"frame":[10,10]})" "\n");
    QTest::newRow("no frame size") << QByteArray(R"({"type":"cursor-timeline","version":1})" "\n");
}

void TestCursorTimeline::unusableFilesAreRejected()
{
    QFETCH(QByteArray, content);
    QString error;
    QVERIFY(!CursorTimeline::parse(content, &error));
    QVERIFY(!error.isEmpty());
}

void TestCursorTimeline::damagedShapeDegradesToUnknown()
{
    const QByteArray content =
        R"({"type":"cursor-timeline","version":1,"frame":[100,100],"shapes":true})" "\n"
        R"({"type":"shape","id":1,"hotspot":[0,0],"png":"bm90IGEgcG5n"})" "\n"
        R"({"type":"move","t":0,"x":5,"y":6,"visible":true,"shape":1})" "\n"
        R"({"type":"future-record","t":1})" "\n"
        R"({"type":"button","t":2,"button":"button-13","down":true,"x":0,"y":0})" "\n"
        R"({"type":"move","t":3,"x":7,"y":8,"visible":true,"shape":1})" "\n";
    const auto timeline = CursorTimeline::parse(content);
    QVERIFY(timeline);
    QCOMPARE(timeline->droppedShapes, 1);
    QVERIFY(timeline->shapes.empty());
    QCOMPARE(int(timeline->samples.size()), 2);
    // Still playable: the position is there, drawn with a generic pointer.
    QCOMPARE(timeline->samples[0].shapeId, 0u);
    QVERIFY(timeline->buttons.empty());
}

void TestCursorTimeline::sizeLimitStillClosesPresses()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("limit.cursor.jsonl"));
    constexpr qint64 kTinyLimit = 600;
    CursorTimelineWriter writer(path, kTinyLimit);
    QVERIFY(writer.open(info()));
    writer.addButton({0, Qt::LeftButton, true, QPoint(), false});
    for (int i = 0; i < 100; ++i) writer.addSample({i, QPoint(i, i), true, 0});
    QVERIFY(writer.truncated());
    writer.addButton({200, Qt::LeftButton, false, QPoint(), true});
    QVERIFY(writer.finish());
    QVERIFY(writer.bytes() < kTinyLimit + 400);

    const auto timeline = CursorTimeline::parse(readAll(path));
    QVERIFY(timeline);
    QVERIFY(timeline->finished);
    QVERIFY(timeline->truncated);
    QVERIFY(timeline->samples.size() < 100);
    // The press is closed: nothing reads as held after the limit.
    QCOMPARE(timeline->buttonsDownAt(1000), Qt::MouseButtons());
}

void TestCursorTimeline::shapeLimits()
{
    QTemporaryDir dir;
    CursorTimelineWriter writer(dir.filePath(QStringLiteral("s.cursor.jsonl")));
    QVERIFY(writer.open(info()));
    QVERIFY(!writer.addShape({1, QPoint(), arrow(CursorTimelineWriter::kMaxShapeEdge + 1)}));
    QVERIFY(!writer.addShape({0, QPoint(), arrow()}));
    QVERIFY(!writer.addShape({1, QPoint(), QImage()}));
    for (int i = 1; i <= CursorTimelineWriter::kMaxShapes; ++i) {
        QVERIFY(writer.addShape({quint32(i), QPoint(), arrow(8)}));
    }
    QVERIFY(!writer.addShape({quint32(CursorTimelineWriter::kMaxShapes + 1), QPoint(), arrow(8)}));
}

void TestCursorTimeline::queries()
{
    CursorTimeline timeline;
    QVERIFY(!timeline.sampleAt(0));
    timeline.samples = {{100, QPoint(1, 1), true, 0}, {200, QPoint(2, 2), true, 0}};
    timeline.buttons = {{150, Qt::LeftButton, true, QPoint(), false},
                        {160, Qt::RightButton, true, QPoint(), false},
                        {170, Qt::LeftButton, false, QPoint(), false}};
    QCOMPARE(timeline.sampleAt(0)->position, QPoint(1, 1));
    QCOMPARE(timeline.sampleAt(199)->position, QPoint(1, 1));
    QCOMPARE(timeline.sampleAt(200)->position, QPoint(2, 2));
    QCOMPARE(timeline.sampleAt(5000)->position, QPoint(2, 2));
    QCOMPARE(timeline.buttonsDownAt(149), Qt::MouseButtons());
    QCOMPARE(timeline.buttonsDownAt(165), Qt::LeftButton | Qt::RightButton);
    QCOMPARE(timeline.buttonsDownAt(170), Qt::MouseButtons(Qt::RightButton));
}

void TestCursorTimeline::videoPointMapping_data()
{
    QTest::addColumn<QRect>("logicalScreen");
    QTest::addColumn<QRect>("physicalScreen");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<QRect>("region");
    QTest::addColumn<QPointF>("logical");
    QTest::addColumn<QPoint>("video");
    // Retina: logical points double.
    QTest::newRow("2x, screen relative") << QRect(0, 0, 1440, 900) << QRect() << 2.0
                                         << QRect(0, 0, 2880, 1800) << QPointF(100.5, 10) << QPoint(201, 20);
    // Windows 150% on a monitor left of the primary: negative origins translate, never scale.
    QTest::newRow("150%, negative origin") << QRect(-1280, 0, 1280, 720) << QRect(-1920, 0, 1920, 1080) << 1.5
                                           << QRect(-1920, 0, 1920, 1080) << QPointF(-1280, 0) << QPoint(0, 0);
    QTest::newRow("150%, inside") << QRect(-1280, 0, 1280, 720) << QRect(-1920, 0, 1920, 1080) << 1.5
                                  << QRect(-1920, 0, 1920, 1080) << QPointF(-640, 360) << QPoint(960, 540);
    // Off the recorded screen: kept off the frame, not clamped onto its edge.
    QTest::newRow("other screen") << QRect(0, 0, 1920, 1080) << QRect(0, 0, 1920, 1080) << 1.0
                                  << QRect(0, 0, 1920, 1080) << QPointF(2500, 10) << QPoint(2500, 10);
}

void TestCursorTimeline::videoPointMapping()
{
    QFETCH(QRect, logicalScreen);
    QFETCH(QRect, physicalScreen);
    QFETCH(qreal, dpr);
    QFETCH(QRect, region);
    QFETCH(QPointF, logical);
    QFETCH(QPoint, video);
    const WindowFrameMapping mapping{logicalScreen, physicalScreen, dpr, region};
    QCOMPARE(mapping.toVideoPoint(logical), video);
}

void TestCursorTimeline::sidecarFollowsTheRecording()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("SnapTray_Recording_s.mp4"));
    QCOMPARE(CursorTimelineSidecar::pathFor(video), video + QStringLiteral(".cursor.jsonl"));
    QVERIFY(!CursorTimelineSidecar::read(video)); // none: the video still plays
    CursorTimelineSidecar::remove(video);         // no-op

    {
        QFile file(video);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    CursorTimelineWriter writer(CursorTimelineSidecar::pathFor(video));
    QVERIFY(writer.open(info()));
    QVERIFY(writer.finish());
    QVERIFY(QFileInfo::exists(CursorTimelineSidecar::pathFor(video)));
    // Discarding a recording (recovery dialog, preview) takes the sidecar too.
    RecordingSession::discard(video);
    QVERIFY(!QFileInfo::exists(video));
    QVERIFY(!QFileInfo::exists(CursorTimelineSidecar::pathFor(video)));
}

void TestCursorTimeline::reportReplaysEvents()
{
    CursorTimeline timeline;
    timeline.info = info();
    timeline.finished = true;
    timeline.samples = {{0, QPoint(1, 2), true, 1}, {30, QPoint(3, 4), false, 1}};
    timeline.buttons = {{10, Qt::LeftButton, true, QPoint(1, 2), false},
                        {40, Qt::LeftButton, false, QPoint(3, 4), true}};
    const QString summary = timeline.report();
    QVERIFY2(summary.contains(QStringLiteral("2 moves, 1 presses (1 releases by SnapTray), last event at 40 ms")),
             qPrintable(summary));
    QVERIFY(!summary.contains(QStringLiteral("move 1,2")));
    const QStringList lines = timeline.report(true).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 3 + 4);
    QCOMPARE(lines[3], QStringLiteral("0 ms  move 1,2 shape 1"));
    QCOMPARE(lines[4], QStringLiteral("10 ms  left down 1,2"));
    QCOMPARE(lines[5], QStringLiteral("30 ms  move 3,4 shape 1 hidden"));
    QCOMPARE(lines[6], QStringLiteral("40 ms  left up 3,4 (released by SnapTray)"));
}

void TestCursorTimeline::growthStaysWithinTheLimit()
{
    // Constant motion for a minute at 60 Hz (the worst case: every poll moves).
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("g.cursor.jsonl"));
    CursorTimelineWriter writer(path);
    QVERIFY(writer.open(info()));
    const qint64 headerBytes = writer.bytes();
    constexpr int kSamplesPerMinute = 60 * 60;
    for (int i = 0; i < kSamplesPerMinute; ++i) {
        writer.addSample({qint64(i) * 16, QPoint(1000 + i % 900, 500 + i % 500), true, 12});
    }
    QVERIFY(writer.finish());
    const double bytesPerMinute = double(writer.bytes() - headerBytes);
    const double hoursAtLimit = CursorTimelineWriter::kDefaultMaxBytes / bytesPerMinute / 60.0;
    qInfo("pointer sidecar: %.0f KiB per minute of constant motion; the %lld MiB limit lasts %.1f h",
          bytesPerMinute / 1024.0, CursorTimelineWriter::kDefaultMaxBytes / (1024 * 1024), hoursAtLimit);
    // A still pointer writes nothing; constant motion must still fit hours.
    QVERIFY(hoursAtLimit >= 3.0);
}

QTEST_GUILESS_MAIN(TestCursorTimeline)
#include "tst_CursorTimeline.moc"
