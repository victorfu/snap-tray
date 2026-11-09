#include <QtTest>

#include <QImage>
#include <QTemporaryDir>

#include <utility>
#include <vector>

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
    void press(int key, Qt::KeyboardModifiers modifiers = {}, const QString& text = {}, qint64 ageMs = 0)
    {
        emit keyPressed(key, modifiers, text, ageMs);
    }
    // Presses the system has queued but the probe has not read yet.
    void poll() override
    {
        for (const auto& [key, modifiers] : std::exchange(queued, {})) press(key, modifiers);
    }
    void setTypedTextWanted(bool wanted) override { typedTextWanted = wanted; }
    bool secure = false;
    int typedTextWanted = -1;
    std::vector<std::pair<int, Qt::KeyboardModifiers>> queued;
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
    void policyKeepsShortcutsThatMoveFocus();
    void policyDropsTextAnInputMethodComposes();
    void labels();
    void writerRoundTripAndDamage();
    void textOnlyFromAllKeysRecordings();
    void recorderRespectsModeSecureInputAndPause();
    void recorderOffWritesNothing();
    void recorderDropsKeysPressedWhilePaused();
    void overlayGroupsAndCounts();
    void overlayLongTypingRun();
    void overlayRepeatRunsRespectBoundaries();
    void benchmarkOverlayLookup_data();
    void benchmarkOverlayLookup();
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
    // The logo key by its name on each system (#210: Windows showed "Super").
    stroke = {};
    stroke.key = Qt::Key_E;
    stroke.modifiers = Qt::MetaModifier;
#ifdef Q_OS_WIN
    QCOMPARE(keyStrokeLabel(stroke, false), QStringLiteral("Win + E"));
#else
    QCOMPARE(keyStrokeLabel(stroke, false), QStringLiteral("Super + E"));
#endif
}

// #210: Alt+Tab and Alt+F4 move the focus themselves, and every key whose
// focus moved before it was read was dropped. A shortcut types nothing, so
// one pressed outside a secure field is kept; typing and AltGr (Ctrl+Alt,
// which types characters on many layouts) are still dropped.
void TestKeyTimeline::policyKeepsShortcutsThatMoveFocus()
{
    using KeyCapturePolicy::keepAfterFocusChange;
    QVERIFY(keepAfterFocusChange(Qt::AltModifier, false));                         // Alt+Tab, Alt+F4
    QVERIFY(keepAfterFocusChange(Qt::MetaModifier, false));                        // Win+E
    QVERIFY(keepAfterFocusChange(Qt::ControlModifier, false));                     // Ctrl+W
    QVERIFY(keepAfterFocusChange(Qt::ControlModifier | Qt::ShiftModifier, false)); // Ctrl+Shift+T
    QVERIFY(!keepAfterFocusChange(Qt::NoModifier, false));                         // typing
    QVERIFY(!keepAfterFocusChange(Qt::ShiftModifier, false));
    QVERIFY(!keepAfterFocusChange(Qt::ControlModifier | Qt::AltModifier, false));  // AltGr
    QVERIFY(!keepAfterFocusChange(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier, false)); // Win+AltGr
    QVERIFY(!keepAfterFocusChange(Qt::NoModifier, true));
    QVERIFY(!keepAfterFocusChange(Qt::AltModifier, true));                         // from a secure field
}

// #210: with an input method composing (Chinese, Japanese, Korean...), the
// raw keys are not what is typed; only shortcuts and named keys are kept.
void TestKeyTimeline::policyDropsTextAnInputMethodComposes()
{
    using KeyCapturePolicy::keepWhileInputMethodComposes;
    QVERIFY(!keepWhileInputMethodComposes(Qt::Key_J, Qt::NoModifier));
    QVERIFY(!keepWhileInputMethodComposes(Qt::Key_3, Qt::ShiftModifier));
    QVERIFY(keepWhileInputMethodComposes(Qt::Key_Return, Qt::NoModifier));
    QVERIFY(keepWhileInputMethodComposes(Qt::Key_Escape, Qt::NoModifier));
    QVERIFY(keepWhileInputMethodComposes(Qt::Key_C, Qt::ControlModifier));
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
    file.close(); // Windows cannot remove an open file
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
    QCOMPARE(probe->typedTextWanted, 0); // shortcuts only: the probe can skip text-only work
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

// PR #200 review: keys pressed while paused but read only after resume
// (still queued, or delivered late) are not recorded.
void TestKeyTimeline::recorderDropsKeysPressedWhilePaused()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("v.mp4.keys.jsonl"));
    auto owned = std::make_unique<FakeKeyProbe>();
    FakeKeyProbe* probe = owned.get();
    qint64 now = 0;
    KeyTimelineRecorder recorder(std::move(owned), [&now] { return now; }, KeyRecordingMode::AllKeys, path, false);
    QCOMPARE(probe->typedTextWanted, 1);
    QVERIFY(recorder.start());
    now = 100;
    recorder.pause(); // media time stops at 100
    probe->queued.push_back({Qt::Key_P, Qt::ControlModifier}); // still queued at resume
    recorder.resume();
    now = 150;
    probe->press(Qt::Key_Q, Qt::ControlModifier, {}, 2000); // pressed while paused, read late
    probe->press(Qt::Key_R, Qt::ControlModifier, {}, 50);   // pressed just as recording resumed
    now = 200;
    probe->press(Qt::Key_S, Qt::ControlModifier);
    recorder.stop();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto timeline = KeyTimeline::parse(file.readAll());
    QVERIFY(timeline);
    QCOMPARE(int(timeline->strokes.size()), 2);
    QCOMPARE(timeline->strokes[0].key, int(Qt::Key_R));
    QCOMPARE(timeline->strokes[0].tMs, 100);
    QCOMPARE(timeline->strokes[1].key, int(Qt::Key_S));
    QCOMPARE(timeline->strokes[1].tMs, 200);
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

void TestKeyTimeline::overlayLongTypingRun()
{
    KeyTimeline timeline;
    timeline.info.mode = KeyRecordingMode::AllKeys;
    constexpr int kStrokeCount = 20000;
    for (int i = 0; i < kStrokeCount; ++i) {
        // aa, bb, cc, dd, ee, ... with no gap large enough to start a new hint.
        const int letter = (i / 2) % 5;
        timeline.strokes.push_back({100 + i * 100, Qt::Key_A + letter, {}, QString(QChar('a' + letter))});
    }
    const KeyOverlayRenderer renderer(timeline, {});
    const QStringList newest{QStringLiteral("b \u00D72"), QStringLiteral("c \u00D72"),
                             QStringLiteral("d \u00D72"), QStringLiteral("e \u00D72")};
    const qint64 last = timeline.strokes.back().tMs;
    QCOMPARE(renderer.tokensAt(last), newest);
    QCOMPARE(renderer.opacityAt(last), 1.0); // fading still uses the group's first key
    // Seeking backwards must neither count future repeats nor change later results.
    QCOMPARE(renderer.tokensAt(100), QStringList{QStringLiteral("a")});
    QCOMPARE(renderer.tokensAt(200), QStringList{QStringLiteral("a \u00D72")});
    QCOMPARE(renderer.tokensAt(last - 100).last(), QStringLiteral("e"));
    QCOMPARE(renderer.tokensAt(last), newest);
    QCOMPARE(renderer.opacityAt(last + 1400), 0.5);
    QVERIFY(renderer.tokensAt(last + 1500).isEmpty());
}

void TestKeyTimeline::overlayRepeatRunsRespectBoundaries()
{
    const KeyTimeline timeline = timelineOf({{100, Qt::Key_A, {}, QStringLiteral("a")},
                                             {200, Qt::Key_A, {}, QStringLiteral("a")},
                                             {300, Qt::Key_A, {}, QStringLiteral("a")},
                                             {400, Qt::Key_A, {}, QStringLiteral("a")},
                                             {1200, Qt::Key_A, {}, QStringLiteral("a")},
                                             {1999, Qt::Key_A, {}, QStringLiteral("a")},
                                             {2799, Qt::Key_A, {}, QStringLiteral("a")}});
    KeyOverlayRenderer renderer(timeline, {});
    QCOMPARE(renderer.tokensAt(450), QStringList{QStringLiteral("a \u00D74")});
    // A gap of 800 ms starts a new group, even if its label is unchanged.
    QCOMPARE(renderer.tokensAt(1200), QStringList{QStringLiteral("a")});
    QCOMPARE(renderer.opacityAt(1200), 0.0);
    QCOMPARE(renderer.tokensAt(1999), QStringList{QStringLiteral("a \u00D72")});
    QCOMPARE(renderer.tokensAt(2799), QStringList{QStringLiteral("a")});

    renderer.setTimeline(TimelineMap({{250, 500, 1.0}}));
    QVERIFY(renderer.tokensAt(250).isEmpty());
    QCOMPARE(renderer.tokensAt(300), QStringList{QStringLiteral("a")});
    QCOMPARE(renderer.opacityAt(300), 0.0);
    QCOMPARE(renderer.opacityAt(340), 0.5);
    QCOMPARE(renderer.tokensAt(450), QStringList{QStringLiteral("a \u00D72")});
    QVERIFY(renderer.tokensAt(500).isEmpty());

    // A cut through a repeated-key run must reset both its count and fade.
    renderer.setTimeline(TimelineMap({{100, 250, 1.0}, {350, 500, 1.0}}));
    QVERIFY(renderer.tokensAt(300).isEmpty());
    QVERIFY(renderer.tokensAt(350).isEmpty());
    QCOMPARE(renderer.tokensAt(450), QStringList{QStringLiteral("a")});
    QCOMPARE(renderer.opacityAt(400), 0.0);
    QCOMPARE(renderer.tokensAt(200), QStringList{QStringLiteral("a \u00D72")});
    renderer.setTimeline(TimelineMap());
    QCOMPARE(renderer.tokensAt(450), QStringList{QStringLiteral("a \u00D74")});
}

void TestKeyTimeline::benchmarkOverlayLookup_data()
{
    QTest::addColumn<int>("strokeCount");
    QTest::addColumn<bool>("repeated");
    QTest::newRow("1000 alternating") << 1000 << false;
    QTest::newRow("100000 alternating") << 100000 << false;
    QTest::newRow("1000 repeated") << 1000 << true;
    QTest::newRow("100000 repeated") << 100000 << true;
}

void TestKeyTimeline::benchmarkOverlayLookup()
{
    QFETCH(int, strokeCount);
    QFETCH(bool, repeated);
    KeyTimeline timeline;
    timeline.info.mode = KeyRecordingMode::AllKeys;
    timeline.strokes.reserve(size_t(strokeCount));
    for (int i = 0; i < strokeCount; ++i) {
        const int letter = repeated ? 0 : i % 2;
        timeline.strokes.push_back({i * 100, Qt::Key_A + letter, {}, QString(QChar('a' + letter))});
    }
    KeyOverlayRenderer renderer(timeline, {});
    const qint64 at = timeline.strokes.back().tMs + 100;
    renderer.setTimeline(TimelineMap({{50, at + 1500, 1.0}}));
    QStringList tokens;
    double opacity = 0.0;
    QBENCHMARK {
        tokens = renderer.tokensAt(at);
        opacity = renderer.opacityAt(at);
    }
    QCOMPARE(opacity, 1.0);
    if (repeated) {
        QCOMPARE(tokens, QStringList{QStringLiteral("a \u00D7%1").arg(strokeCount - 1)});
    } else {
        QCOMPARE(tokens, (QStringList{QStringLiteral("a"), QStringLiteral("b"),
                                     QStringLiteral("a"), QStringLiteral("b")}));
    }
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
