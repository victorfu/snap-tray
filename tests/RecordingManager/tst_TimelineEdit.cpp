#include <QtTest>

#include "recording/TimelineEdit.h"

using namespace SnapTray;

class TestTimelineEdit : public QObject
{
    Q_OBJECT

private slots:
    void emptyEditIsThePlainTrim();
    void splitDeleteAndSpeed();
    void mapsBothWays();
    void neighboursAtOneSpeedMerge();
    void splitLimits();
    void playbackSkipsCuts();
    void jsonRoundTrip();
};

void TestTimelineEdit::emptyEditIsThePlainTrim()
{
    const TimelineEdit edit;
    QVERIFY(edit.isEmpty());
    const TimelineMap map = edit.map(300, 900);
    QVERIFY(map.isPlainTrim());
    QCOMPARE(map.outputDurationMs(), 600);
    QCOMPARE(*map.sourceToOutput(300), 0);
    QCOMPARE(*map.sourceToOutput(899), 599);
    QVERIFY(!map.sourceToOutput(299));
    QVERIFY(!map.sourceToOutput(900));
    QCOMPARE(map.outputToSource(100), 400);
}

void TestTimelineEdit::splitDeleteAndSpeed()
{
    TimelineEdit edit;
    QVERIFY(edit.splitAt(1000, 5000));
    QVERIFY(edit.splitAt(2000, 5000));
    QVERIFY(edit.splitAt(3000, 5000));
    QCOMPARE(int(edit.parts.size()), 4);
    QCOMPARE(edit.partAt(500), 0);
    QCOMPARE(edit.partAt(1000), 1);
    QCOMPARE(edit.partAt(2999), 2);
    QVERIFY(edit.setDeleted(1, true));    // cut 1000-2000
    QVERIFY(edit.setSpeed(2, 2.0));       // 2000-3000 at 2x
    QVERIFY(!edit.setSpeed(2, 3.0));      // not a choice
    QVERIFY(!edit.isEmpty());
    const TimelineMap map = edit.map(0, 5000);
    QCOMPARE(int(map.segments().size()), 3);
    // 1000 + 500 + 2000.
    QCOMPARE(map.outputDurationMs(), 3500);
    QVERIFY(!map.isPlainTrim());
    // Splitting keeps the style for both halves; joining keeps the earlier one.
    QVERIFY(edit.splitAt(2500, 5000));
    QCOMPARE(edit.parts[2].speed, 2.0);
    QCOMPARE(edit.parts[3].speed, 2.0);
    QVERIFY(edit.removeSplit(2));
    QCOMPARE(int(edit.parts.size()), 4);
    QCOMPARE(edit.map(0, 5000).outputDurationMs(), 3500);
}

void TestTimelineEdit::mapsBothWays()
{
    TimelineEdit edit;
    edit.splitAt(1000, 5000);
    edit.splitAt(2000, 5000);
    edit.splitAt(3000, 5000);
    edit.setDeleted(1, true);
    edit.setSpeed(2, 2.0);
    edit.setSpeed(3, 0.5);
    // Trim 500-4000: kept 500-1000 (1x), 2000-3000 (2x), 3000-4000 (0.5x).
    const TimelineMap map = edit.map(500, 4000);
    QCOMPARE(map.outputDurationMs(), 500 + 500 + 2000);
    QCOMPARE(*map.sourceToOutput(500), 0);
    QCOMPARE(*map.sourceToOutput(999), 499);
    QVERIFY(!map.sourceToOutput(1500)); // cut out
    QCOMPARE(*map.sourceToOutput(2000), 500);
    QCOMPARE(*map.sourceToOutput(2500), 750);
    QCOMPARE(*map.sourceToOutput(3000), 1000);
    QCOMPARE(*map.sourceToOutput(3500), 2000);
    QCOMPARE(map.outputToSource(0), 500);
    QCOMPARE(map.outputToSource(750), 2500);
    QCOMPARE(map.outputToSource(2000), 3500);
    QCOMPARE(map.outputToSource(99999), 4000);
    // Round trip for every kept millisecond.
    for (qint64 out = 0; out < map.outputDurationMs(); out += 7) {
        const qint64 source = map.outputToSource(out);
        const auto back = map.sourceToOutput(source);
        QVERIFY(back);
        QVERIFY2(qAbs(*back - out) <= 1, qPrintable(QStringLiteral("%1 -> %2 -> %3").arg(out).arg(source).arg(*back)));
    }
    QCOMPARE(map.speedAt(2500), 2.0);
    QCOMPARE(map.speedAt(1500), 1.0);
    QCOMPARE(map.segmentAt(1500), -1);
    QCOMPARE(map.segmentOutputStartMs(2), 1000);
}

void TestTimelineEdit::neighboursAtOneSpeedMerge()
{
    TimelineEdit edit;
    edit.splitAt(1000, 3000);
    // A split nobody used changes nothing in the export.
    QVERIFY(edit.map(0, 3000).isPlainTrim());
    QVERIFY(!edit.isEmpty()); // but it is an edit (undo sees it)
}

void TestTimelineEdit::splitLimits()
{
    TimelineEdit edit;
    QVERIFY(!edit.splitAt(50, 5000));   // too close to the start
    QVERIFY(!edit.splitAt(4950, 5000)); // too close to the end
    QVERIFY(edit.splitAt(1000, 5000));
    QVERIFY(!edit.splitAt(1000, 5000)); // already a split
    QVERIFY(!edit.splitAt(1050, 5000)); // part too short
    QVERIFY(edit.splitAt(2000, 5000));
    QVERIFY(edit.moveSplit(0, 1500, 5000));
    QVERIFY(!edit.moveSplit(0, 1950, 5000)); // would make the next part too short
    QVERIFY(!edit.moveSplit(1, 1400, 5000)); // would cross the previous split
    QCOMPARE(edit.splits, (std::vector<qint64>{1500, 2000}));
    QVERIFY(!edit.removeSplit(5));
}

void TestTimelineEdit::playbackSkipsCuts()
{
    TimelineEdit edit;
    edit.splitAt(1000, 4000);
    edit.splitAt(2000, 4000);
    edit.setDeleted(1, true);
    const TimelineMap map = edit.map(0, 3000);
    QCOMPARE(map.nextShownSourceMs(500), 500);
    QCOMPARE(map.nextShownSourceMs(1000), 2000);
    QCOMPARE(map.nextShownSourceMs(1999), 2000);
    QCOMPARE(map.nextShownSourceMs(3000), -1);
}

void TestTimelineEdit::jsonRoundTrip()
{
    TimelineEdit edit;
    edit.splitAt(1000, 5000);
    edit.splitAt(2500, 5000);
    edit.setDeleted(0, true);
    edit.setSpeed(2, 4.0);
    QCOMPARE(TimelineEdit::fromJson(edit.toJson()), edit);
    // Damaged data never yields a half-valid edit.
    QJsonObject broken = edit.toJson();
    broken.insert(QStringLiteral("splits"), QJsonArray{2500, 1000});
    QVERIFY(TimelineEdit::fromJson(broken).isEmpty());
    QVERIFY(TimelineEdit::fromJson({}).isEmpty());
}

QTEST_MAIN(TestTimelineEdit)
#include "tst_TimelineEdit.moc"
