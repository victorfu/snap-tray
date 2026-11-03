#include <QtTest>

#include <QImage>
#include <QTemporaryDir>

#include "recording/KeyOverlay.h"
#include "recording/KeyTimeline.h"
#include "recording/KeyTimelineRecorder.h"

using namespace SnapTray;

namespace {

class FakeKeyProbe : public KeyProbe
{
public:
    QString name() const override { return QStringLiteral("fake"); }
    bool secureInputActive() const override { return secure; }
    bool detectsSecureInput() const override { return true; }
    void press(int key, Qt::KeyboardModifiers modifiers = {}, const QString& text = {})
    {
        emit keyPressed(key, modifiers, text);
    }
    bool secure = false;
};

KeyTimeline timelineOf(std::vector<KeyStroke> strokes)
{
    KeyTimeline t;
    t.strokes = std::move(strokes);
    return t;
}

} // namespace

class TestKeyTimeline : public QObject
{
    Q_OBJECT

private slots:
    void policyKeepsOnlyWhatTheModeAllows();
    void labels();
    void writerRoundTripAndDamage();
    void textOnlyFromAllKeysRecordings();
    void recorderRespectsModeSecureInputAndPause();
    void recorderOffWritesNothing();
    void overlayGroupsAndCounts();
    void overlayTimingIsAFunctionOfTime();
    void overlayNeverReachesAcrossACut();
    void overlayDrawsWhereItSays();
};

void TestKeyTimeline::policyKeepsOnlyWhatTheModeAllows()
{
    using namespace KeyCapturePolicy;
    const auto shortcuts = KeyRecordingMode::Shortcuts;
    QVERIFY(!shouldRecord(KeyRecordingMode::Off, Qt::Key_C, Qt::ControlModifier, false));
    QVERIFY(shouldRecord(shortcuts, Qt::Key_C, Qt::ControlModifier, false));
    QVERIFY(shouldRecord(shortcuts, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier, false));
    QVERIFY(shouldRecord(shortcuts, Qt::Key_Tab, Qt::AltModifier, false));
    QVERIFY(shouldRecord(shortcuts, Qt::Key_Return, Qt::NoModifier, false));
    QVERIFY(shouldRecord(shortcuts, Qt::Key_F5, Qt::NoModifier, false));
    QVERIFY(shouldRecord(shortcuts, Qt::Key_Escape, Qt::NoModifier, false));
    // Typing is not a shortcut: letters, Shift+letter, digits, space.
    QVERIFY(!shouldRecord(shortcuts, Qt::Key_A, Qt::NoModifier, false));
    QVERIFY(!shouldRecord(shortcuts, Qt::Key_A, Qt::ShiftModifier, false));
    QVERIFY(!shouldRecord(shortcuts, Qt::Key_5, Qt::NoModifier, false));
    QVERIFY(!shouldRecord(shortcuts, Qt::Key_Space, Qt::NoModifier, false));
    // A modifier alone is never a stroke.
    QVERIFY(!shouldRecord(KeyRecordingMode::AllKeys, Qt::Key_Control, Qt::ControlModifier, false));
    QVERIFY(shouldRecord(KeyRecordingMode::AllKeys, Qt::Key_A, Qt::NoModifier, false));
    // Secure input: nothing, in any mode.
    QVERIFY(!shouldRecord(shortcuts, Qt::Key_C, Qt::ControlModifier, true));
    QVERIFY(!shouldRecord(KeyRecordingMode::AllKeys, Qt::Key_A, Qt::NoModifier, true));
}

void TestKeyTimeline::labels()
{
    KeyStroke stroke;
    stroke.key = Qt::Key_T;
    stroke.modifiers = Qt::ControlModifier | Qt::ShiftModifier;
    QCOMPARE(keyStrokeLabel(stroke, false), QStringLiteral("Ctrl + Shift + T"));
    QCOMPARE(keyStrokeLabel(stroke, true), QStringLiteral("\u21E7 \u2318 T"));
    stroke = {};
    stroke.key = Qt::Key_Return;
    QVERIFY(!keyStrokeLabel(stroke, false).isEmpty());
    stroke = {};
    stroke.key = Qt::Key_A;
    stroke.text = QStringLiteral("a");
    QCOMPARE(keyStrokeLabel(stroke, false), QStringLiteral("a"));
}

void TestKeyTimeline::writerRoundTripAndDamage()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("v.mp4"));
    {
        KeyTimelineWriter writer(KeyTimelineSidecar::pathFor(video));
        QVERIFY(writer.open({KeyRecordingMode::Shortcuts, true}));
        writer.add({100, Qt::Key_C, Qt::ControlModifier, {}});
        writer.add({250, Qt::Key_V, Qt::ControlModifier, {}});
        QVERIFY(writer.finish());
    }
    const auto timeline = KeyTimelineSidecar::read(video);
    QVERIFY(timeline);
    QVERIFY(timeline->finished);
    QVERIFY(timeline->info.macSymbols);
    QCOMPARE(int(timeline->strokes.size()), 2);
    QCOMPARE(timeline->strokes[1].key, int(Qt::Key_V));
    // Nothing but times, keys and modifiers: no window titles, no applications.
    QFile file(KeyTimelineSidecar::pathFor(video));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    for (const char* forbidden : {"title", "window", "app", "process"}) QVERIFY(!content.contains(forbidden));
    // A torn last line (a crash) keeps what came before.
    QByteArray torn = content;
    torn.chop(20);
    const auto partial = KeyTimeline::parse(torn);
    QVERIFY(partial);
    QVERIFY(!partial->finished);
    QVERIFY(partial->strokes.size() >= 1);
    QVERIFY(!KeyTimeline::parse("{\"type\":\"other\"}\n"));
    KeyTimelineSidecar::remove(video);
    QVERIFY(!QFile::exists(KeyTimelineSidecar::pathFor(video)));
}

void TestKeyTimeline::textOnlyFromAllKeysRecordings()
{
    // A shortcuts-only file never yields typed text, even if a line has some.
    const QByteArray shortcuts = "{\"type\":\"snaptray-keys\",\"version\":1,\"mode\":\"shortcuts\"}\n"
                                 "{\"type\":\"key\",\"t\":10,\"key\":65,\"mods\":0,\"text\":\"a\"}\n";
    const auto parsed = KeyTimeline::parse(shortcuts);
    QVERIFY(parsed);
    QCOMPARE(int(parsed->strokes.size()), 1);
    QVERIFY(parsed->strokes[0].text.isEmpty());
}

void TestKeyTimeline::recorderRespectsModeSecureInputAndPause()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("v.mp4.keys.jsonl"));
    auto owned = std::make_unique<FakeKeyProbe>();
    FakeKeyProbe* probe = owned.get();
    qint64 now = 0;
    KeyTimelineRecorder recorder(std::move(owned), [&now] { return now; }, KeyRecordingMode::Shortcuts, path, false);
    QVERIFY(recorder.start());
    now = 100;
    probe->press(Qt::Key_C, Qt::ControlModifier);
    probe->press(Qt::Key_H, Qt::NoModifier, QStringLiteral("h")); // typing
    now = 200;
    probe->secure = true; // a password field
    probe->press(Qt::Key_V, Qt::ControlModifier);
    probe->press(Qt::Key_Return);
    probe->secure = false;
    recorder.pause();
    now = 300;
    probe->press(Qt::Key_Z, Qt::ControlModifier);
    recorder.resume();
    now = 400;
    probe->press(Qt::Key_S, Qt::ControlModifier);
    recorder.stop();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto timeline = KeyTimeline::parse(file.readAll());
    QVERIFY(timeline);
    QCOMPARE(int(timeline->strokes.size()), 2);
    QCOMPARE(timeline->strokes[0].tMs, 100);
    QCOMPARE(timeline->strokes[0].key, int(Qt::Key_C));
    QCOMPARE(timeline->strokes[1].tMs, 400);
    QCOMPARE(timeline->strokes[1].key, int(Qt::Key_S));
}

void TestKeyTimeline::recorderOffWritesNothing()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("v.mp4.keys.jsonl"));
    KeyTimelineRecorder recorder(std::make_unique<FakeKeyProbe>(), [] { return qint64(0); }, KeyRecordingMode::Off,
                                 path, false);
    QVERIFY(!recorder.start());
    QVERIFY(!QFile::exists(path));
}

void TestKeyTimeline::overlayGroupsAndCounts()
{
    const KeyTimeline t = timelineOf({{1000, Qt::Key_Z, Qt::ControlModifier, {}},
                                      {1300, Qt::Key_Z, Qt::ControlModifier, {}},
                                      {1600, Qt::Key_Z, Qt::ControlModifier, {}},
                                      {1900, Qt::Key_S, Qt::ControlModifier, {}},
                                      {5000, Qt::Key_Return, Qt::NoModifier, {}}});
    const KeyOverlayRenderer renderer(t, {});
    QVERIFY(renderer.tokensAt(999).isEmpty());
    QCOMPARE(renderer.tokensAt(1000), QStringList{QStringLiteral("Ctrl + Z")});
    QCOMPARE(renderer.tokensAt(1700), QStringList{QStringLiteral("Ctrl + Z \u00D73")});
    QCOMPARE(renderer.tokensAt(2000), (QStringList{QStringLiteral("Ctrl + Z \u00D73"), QStringLiteral("Ctrl + S")}));
    // Gone 1.5 s after the last key of the group; the next one shows alone.
    QVERIFY(renderer.tokensAt(1900 + 1500).isEmpty());
    QCOMPARE(renderer.tokensAt(5100).size(), 1);
    KeyOverlaySettings off;
    off.enabled = false;
    QVERIFY(KeyOverlayRenderer(t, off).tokensAt(1000).isEmpty());
}

void TestKeyTimeline::overlayTimingIsAFunctionOfTime()
{
    const KeyTimeline t = timelineOf({{1000, Qt::Key_C, Qt::ControlModifier, {}}});
    const KeyOverlayRenderer renderer(t, {});
    QCOMPARE(renderer.opacityAt(1000), 0.0);
    QCOMPARE(renderer.opacityAt(1000 + KeyOverlaySettings::kFadeInMs), 1.0);
    QVERIFY(qAbs(renderer.opacityAt(2500 - KeyOverlaySettings::kFadeOutMs / 2) - 0.5) < 0.01);
    const double a = renderer.opacityAt(2400);
    renderer.opacityAt(9000);
    QCOMPARE(renderer.opacityAt(2400), a);
}

void TestKeyTimeline::overlayNeverReachesAcrossACut()
{
    // Kept: 0-1000 and 2000-3000; the shortcut is pressed in the cut part.
    const KeyTimeline t = timelineOf({{1500, Qt::Key_C, Qt::ControlModifier, {}},
                                      {2200, Qt::Key_V, Qt::ControlModifier, {}}});
    KeyOverlayRenderer renderer(t, {});
    QVERIFY(!renderer.tokensAt(2100).isEmpty()); // without the cut, Ctrl + C would still show
    renderer.setTimeline(TimelineMap({{0, 1000, 1.0}, {2000, 3000, 1.0}}));
    QVERIFY(renderer.tokensAt(2100).isEmpty());
    // Ctrl + V shows alone: no group across the cut.
    QCOMPARE(renderer.tokensAt(2300), QStringList{QStringLiteral("Ctrl + V")});
}

void TestKeyTimeline::overlayDrawsWhereItSays()
{
    const KeyTimeline t = timelineOf({{0, Qt::Key_C, Qt::ControlModifier, {}}});
    QImage base(400, 200, QImage::Format_RGB32);
    base.fill(QColor(40, 120, 200));
    const auto changedRows = [&](const QImage& frame) {
        int top = -1, bottom = -1;
        for (int y = 0; y < frame.height(); ++y) {
            for (int x = 0; x < frame.width(); ++x) {
                if (frame.pixel(x, y) != base.pixel(x, y)) {
                    if (top < 0) top = y;
                    bottom = y;
                    break;
                }
            }
        }
        return qMakePair(top, bottom);
    };
    QImage bottom = base;
    KeyOverlayRenderer(t, {}).paint(bottom, 500);
    const auto rows = changedRows(bottom);
    QVERIFY(rows.first > 100); // bottom half
    KeyOverlaySettings topSettings;
    topSettings.position = KeyOverlaySettings::Position::TopCenter;
    QImage top = base;
    KeyOverlayRenderer(t, topSettings).paint(top, 500);
    QVERIFY(changedRows(top).second < 100);
    // The same hint at twice the size draws the same layout, scaled.
    QImage big(800, 400, QImage::Format_RGB32);
    big.fill(QColor(40, 120, 200));
    const QImage bigBase = big;
    KeyOverlayRenderer(t, {}).paint(big, 500);
    int bigTop = -1;
    for (int y = 0; y < big.height() && bigTop < 0; ++y)
        for (int x = 0; x < big.width(); ++x)
            if (big.pixel(x, y) != bigBase.pixel(x, y)) { bigTop = y; break; }
    QVERIFY(qAbs(bigTop - 2 * rows.first) <= 4);
    // Nothing outside the hint's time.
    QImage later = base;
    KeyOverlayRenderer(t, {}).paint(later, 5000);
    QCOMPARE(later, base);
}

QTEST_MAIN(TestKeyTimeline)
#include "tst_KeyTimeline.moc"
