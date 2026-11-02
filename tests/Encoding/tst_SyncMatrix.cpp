#include <QtTest>

#include <QTemporaryDir>

#ifdef SNAPTRAY_SYNC_MATRIX_DECODE
#include "SyncMatrix.h"
#endif

// The recording sync matrix (#163). Every row records synthetic content
// through the real timing pipeline into the platform encoder, then decodes
// the file. "pass" rows must decode correctly; "detect" rows inject a bug
// or a fault the pipeline does not correct, and the analysis must report it.
// SNAPTRAY_SYNC_FULL=1 adds 4K and long rows; SNAPTRAY_SYNC_REPORT_DIR keeps
// a JSON report per row for A/B comparison (scripts/sync-matrix-compare.py).
class TestSyncMatrix : public QObject
{
    Q_OBJECT

private slots:
    void matrix_data();
    void matrix();
};

#ifdef SNAPTRAY_SYNC_MATRIX_DECODE

namespace {

using namespace SyncMatrix;

struct Row {
    Scenario scenario;
    bool mustPass;      // false: the analysis must report a failure
    bool full = false;  // only with SNAPTRAY_SYNC_FULL=1
};

QVector<Row> rows()
{
    QVector<Row> list;
    const auto add = [&list](Scenario s, bool mustPass = true, bool full = false) { list.push_back({s, mustPass, full}); };
    Scenario base;
    {
        auto s = base; s.name = "clean-360p30-mic"; add(s);
        s = base; s.name = "clean-360p60-none"; s.fps = 60; s.audio = Audio::None; add(s);
        s = base; s.name = "clean-360p30-system"; s.audio = Audio::SystemAudio; add(s);
        s = base; s.name = "clean-1080p30-mixed"; s.size = QSize(1920, 1080); s.durationMs = 2000; s.audio = Audio::Mixed; add(s);
        s = base; s.name = "clean-1080p60-mixed"; s.size = QSize(1920, 1080); s.fps = 60; s.durationMs = 2000; s.audio = Audio::Mixed; add(s);
    }
    {
        auto s = base; s.name = "jitter-and-bursts"; s.sourceJitterMs = 8; s.arrivalDelayMs = 40; s.burstEvery = 5; add(s);
        s = base; s.name = "drops-duplicates-backward"; s.dropEvery = 7; s.duplicateEvery = 5; s.backwardEvery = 11; add(s);
        s = base; s.name = "early-and-repeated-pauses"; s.audio = Audio::Mixed;
        s.pauses = {{150, 400}, {1600, 300}, {2200, 250}}; s.durationMs = 3500; add(s);
        s = base; s.name = "static-screen"; s.staticFromMs = 1000; s.staticToMs = 2000; add(s);
        s = base; s.name = "audio-gap-one-source"; s.audio = Audio::Mixed; s.audioGapFromMs = 900; s.audioGapToMs = 1300; add(s);
        s = base; s.name = "audio-clock-offset-and-drift"; s.audioClockOffsetMs = 3; s.audioDriftPpm = 300; add(s);
        s = base; s.name = "slow-encoder"; s.slowEncoderMs = 150; add(s);
    }
    {
        // The analysis must catch these.
        auto s = base; s.name = "detect-ignored-pause"; s.pauses = {{1000, 500}}; s.mutation = Mutation::IgnorePause; add(s, false);
        s = base; s.name = "detect-shifted-audio"; s.mutation = Mutation::ShiftAudio; add(s, false);
        s = base; s.name = "detect-truncated-tail"; s.mutation = Mutation::TruncateTail; add(s, false);
        s = base; s.name = "detect-reordered-timestamps"; s.mutation = Mutation::ReorderTimestamps; add(s, false);
        s = base; s.name = "detect-late-audio-engine"; s.audioClockOffsetMs = 150; add(s, false);
    }
    {
        auto s = base; s.name = "full-2160p30-mixed"; s.size = QSize(3840, 2160); s.durationMs = 3000; s.audio = Audio::Mixed; add(s, true, true);
        s = base; s.name = "full-2160p60-mic"; s.size = QSize(3840, 2160); s.fps = 60; s.durationMs = 2000; add(s, true, true);
        const qint64 longSeconds = qEnvironmentVariableIntValue("SNAPTRAY_SYNC_LONG_S") > 0
            ? qEnvironmentVariableIntValue("SNAPTRAY_SYNC_LONG_S") : 120;
        s = base; s.name = "full-long-1080p30-mixed"; s.size = QSize(1920, 1080); s.durationMs = longSeconds * 1000;
        s.audio = Audio::Mixed; s.pauses = {{longSeconds * 400, 2000}}; s.sourceJitterMs = 4; add(s, true, true);
    }
    return list;
}

} // namespace

void TestSyncMatrix::matrix_data()
{
    QTest::addColumn<int>("row");
    const auto all = rows();
    for (int i = 0; i < all.size(); ++i) QTest::newRow(qPrintable(all[i].scenario.name)) << i;
}

void TestSyncMatrix::matrix()
{
    QFETCH(int, row);
    const Row r = rows().at(row);
    if (r.full && qEnvironmentVariable("SNAPTRAY_SYNC_FULL") != QLatin1String("1")) {
        QSKIP("4K and long rows run with SNAPTRAY_SYNC_FULL=1");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Result result = run(r.scenario, dir.path());
    writeReport(r.scenario, result);
    if (!result.skipped.isEmpty()) QSKIP(qPrintable(result.skipped.join(QStringLiteral("; "))));
    qInfo().noquote() << r.scenario.name << "measured" << QJsonDocument(result.measured).toJson(QJsonDocument::Compact);
    qInfo().noquote() << r.scenario.name << "cost" << QJsonDocument(result.cost).toJson(QJsonDocument::Compact);
    if (r.mustPass) {
        QVERIFY2(result.passed(), qPrintable(result.failures.join(QStringLiteral("; "))));
    } else {
        QVERIFY2(!result.passed(), "the injected error was not detected");
        qInfo().noquote() << r.scenario.name << "detected:" << result.failures.join(QStringLiteral("; "));
    }
}

#else

void TestSyncMatrix::matrix_data()
{
    QTest::addColumn<int>("row");
    QTest::newRow("platform") << 0;
}

void TestSyncMatrix::matrix()
{
    // Explicitly unsupported here, never a silent pass: decoding the written
    // media for analysis uses the FFmpeg readers of the Linux build.
    QSKIP("Decoded-media analysis is not available on this platform yet; "
          "see docs/developer/recording-sync-matrix.md");
}

#endif

QTEST_MAIN(TestSyncMatrix)
#include "tst_SyncMatrix.moc"
