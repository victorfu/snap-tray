#include <QtTest/QtTest>

#include "recording/WindowTimeline.h"

using SnapTray::WindowSample;
using SnapTray::WindowTimeline;

namespace {

WindowSample sample(quint32 id, const QRect& rect, int z, const char* app = "App",
                    ElementType type = ElementType::Window)
{
    WindowSample s;
    s.windowId = id;
    s.rect = rect;
    s.z = z;
    s.ownerApp = QString::fromLatin1(app);
    s.type = type;
    return s;
}

} // namespace

class tst_WindowTimeline : public QObject
{
    Q_OBJECT

private slots:
    void appendCompactsUnchangedEntries();
    void windowsAtPicksLastEntryAtOrBefore();
    void windowsAtOutsideRange();
    void hitTestPrefersTopmost();
    void hitTestMissesOutsideEveryWindow();
    void hasWindowsIgnoresEmptyEntries();
    void jsonRoundTrip();
    void fromJsonRejectsBadInput_data();
    void fromJsonRejectsBadInput();
};

void tst_WindowTimeline::appendCompactsUnchangedEntries()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    const std::vector<WindowSample> a{sample(1, QRect(0, 0, 800, 600), 0)};
    const std::vector<WindowSample> b{sample(1, QRect(10, 0, 800, 600), 0)};
    timeline.append(0, a);
    timeline.append(250, a);   // unchanged: dropped
    timeline.append(500, a);   // unchanged: dropped
    timeline.append(750, b);   // moved: kept
    timeline.append(1000, a);  // moved back: kept (differs from the last entry)
    QCOMPARE(timeline.entries().size(), size_t(3));
    QCOMPARE(timeline.entries()[0].tMs, qint64(0));
    QCOMPARE(timeline.entries()[1].tMs, qint64(750));
    QCOMPARE(timeline.entries()[2].tMs, qint64(1000));
    QVERIFY(!timeline.isEmpty());
}

void tst_WindowTimeline::windowsAtPicksLastEntryAtOrBefore()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    timeline.append(0, {sample(1, QRect(0, 0, 100, 100), 0)});
    timeline.append(1000, {sample(2, QRect(0, 0, 100, 100), 0)});
    QCOMPARE(timeline.windowsAt(999)->front().windowId, quint32(1));
    QCOMPARE(timeline.windowsAt(1000)->front().windowId, quint32(2));
    QCOMPARE(timeline.windowsAt(5000)->front().windowId, quint32(2));
}

void tst_WindowTimeline::windowsAtOutsideRange()
{
    WindowTimeline empty;
    QVERIFY(empty.windowsAt(0) == nullptr);
    QVERIFY(!empty.hitTest(QPoint(0, 0), 0).has_value());

    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    timeline.append(40, {sample(7, QRect(0, 0, 100, 100), 0)});
    // Before the first sample (the first frame is stamped a few ms after start): use it.
    QCOMPARE(timeline.windowsAt(0)->front().windowId, quint32(7));
    QCOMPARE(timeline.windowsAt(-5)->front().windowId, quint32(7));
}

void tst_WindowTimeline::hitTestPrefersTopmost()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    // Entry windows are stored in z order; hitTest must not rely on it.
    timeline.append(0, {sample(2, QRect(0, 0, 1920, 1080), 1, "Back"),
                        sample(1, QRect(100, 100, 400, 300), 0, "Front")});
    const auto hit = timeline.hitTest(QPoint(150, 150), 0);
    QVERIFY(hit.has_value());
    QCOMPARE(hit->windowId, quint32(1));
    QCOMPARE(hit->ownerApp, QStringLiteral("Front"));
    const auto back = timeline.hitTest(QPoint(50, 50), 0);
    QVERIFY(back.has_value());
    QCOMPARE(back->windowId, quint32(2));
}

void tst_WindowTimeline::hitTestMissesOutsideEveryWindow()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    timeline.append(0, {sample(1, QRect(100, 100, 400, 300), 0)});
    QVERIFY(!timeline.hitTest(QPoint(50, 50), 0).has_value());
    // QRect::contains is inclusive of right/bottom - 1 only.
    QVERIFY(timeline.hitTest(QPoint(499, 399), 0).has_value());
    QVERIFY(!timeline.hitTest(QPoint(500, 400), 0).has_value());
}

void tst_WindowTimeline::hasWindowsIgnoresEmptyEntries()
{
    WindowTimeline timeline;
    QVERIFY(!timeline.hasWindows());
    timeline.append(0, {});
    QVERIFY(!timeline.isEmpty());
    QVERIFY(!timeline.hasWindows());
    WindowSample sample;
    sample.windowId = 1;
    sample.rect = QRect(0, 0, 10, 10);
    timeline.append(100, {sample});
    QVERIFY(timeline.hasWindows());
}

void tst_WindowTimeline::jsonRoundTrip()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(2560, 1440));
    timeline.append(0, {sample(197172, QRect(0, 0, 1280, 1440), 0, "Code"),
                        sample(5, QRect(1280, 0, 1280, 1440), 1, "Safari", ElementType::Dialog)});
    timeline.append(1250, {sample(197172, QRect(0, 0, 1280, 1440), 0, "Code")});
    const QByteArray json = timeline.toJson();
    QVERIFY(!json.contains("title"));
    const auto parsed = WindowTimeline::fromJson(json);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->frameSize(), QSize(2560, 1440));
    QCOMPARE(parsed->entries().size(), size_t(2));
    QCOMPARE(parsed->entries()[0].windows.size(), size_t(2));
    QVERIFY(parsed->entries()[0].windows[1] == timeline.entries()[0].windows[1]);
    QCOMPARE(parsed->entries()[0].windows[1].type, ElementType::Dialog);
    QCOMPARE(parsed->entries()[1].tMs, qint64(1250));
    QCOMPARE(parsed->toJson(), json);
}

void tst_WindowTimeline::fromJsonRejectsBadInput_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("not json") << QByteArray("hello");
    QTest::newRow("array root") << QByteArray("[]");
    QTest::newRow("wrong version") << QByteArray(R"({"version":2,"frameSize":[1,1],"entries":[]})");
    QTest::newRow("missing frame size") << QByteArray(R"({"version":1,"entries":[]})");
    QTest::newRow("empty frame size") << QByteArray(R"({"version":1,"frameSize":[0,0],"entries":[]})");
    QTest::newRow("rect not four numbers")
        << QByteArray(R"({"version":1,"frameSize":[10,10],"entries":[{"t":0,"windows":[{"id":1,"rect":[1,2,3],"z":0,"app":"a","type":"window"}]}]})");
    QTest::newRow("unknown type")
        << QByteArray(R"({"version":1,"frameSize":[10,10],"entries":[{"t":0,"windows":[{"id":1,"rect":[0,0,5,5],"z":0,"app":"a","type":"menu"}]}]})");
}

void tst_WindowTimeline::fromJsonRejectsBadInput()
{
    QFETCH(QByteArray, json);
    QVERIFY(!WindowTimeline::fromJson(json).has_value());
}

QTEST_GUILESS_MAIN(tst_WindowTimeline)
#include "tst_WindowTimeline.moc"
