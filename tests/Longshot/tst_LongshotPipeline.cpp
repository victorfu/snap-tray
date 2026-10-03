#include "SyntheticScroll.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "longshot/LongshotPipeline.h"

#include <QtTest>
#include <QTemporaryDir>

#include <set>

using namespace SnapTray::Longshot;
using namespace SyntheticScroll;

namespace {

constexpr int kPositionTolerance = 2;
constexpr double kMinPlacedFraction = 0.95;

struct Recording {
    QString path;
    Trajectory trajectory;
    QImage page;
};

// Renders, encodes and returns the recording; empty path on failure (message in error).
Recording record(const QString& path, const PageSpec& spec, const Trajectory& trajectory, const Disturbances& d, QString* error)
{
    Recording r;
    r.page = renderPage(spec);
    r.trajectory = clampTrajectory(trajectory, r.page.height(), kViewport.height());
    std::vector<QImage> frames;
    for (size_t i = 0; i < r.trajectory.offsets.size(); ++i) frames.push_back(renderFrame(r.page, kViewport, r.trajectory, int(i), d));
    *error = encodeFrames(path, frames, kFrameRate);
    if (error->isEmpty()) r.path = path;
    return r;
}

// Fraction of placed frames whose solved position matches the trajectory
// (relative to the first placed frame) within tolerance.
double placedAccuracy(const AnalysisResult& a, const Trajectory& t, int* placedCount)
{
    int placed = 0;
    int accurate = 0;
    int anchorIndex = -1;
    for (size_t i = 0; i < a.solve.positions.size(); ++i) {
        if (!a.solve.positions[i].has_value()) continue;
        if (anchorIndex < 0) anchorIndex = int(i);
        ++placed;
        const int expected = t.offsets[i] - t.offsets[anchorIndex];
        if (qAbs(*a.solve.positions[i] - expected) <= kPositionTolerance) ++accurate;
    }
    *placedCount = placed;
    return placed ? double(accurate) / placed : 0.0;
}

} // namespace

class tst_LongshotPipeline : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void closureCandidatesPreferOverlapAndGap();
    void masksResolvePerFrame();
    void trajectories_data();
    void trajectories();
    void stationaryRecordingYieldsOneFrame();
    void blankGapReportsBreak();
    void tooFewFramesFails();
    void cancelStopsEarly();
    void incrementalReusesKnownFrames();

private:
    QTemporaryDir m_dir;
};

void tst_LongshotPipeline::initTestCase()
{
    QVERIFY(m_dir.isValid());
    if (!FrameReaderLongshotSource::createNative()) QSKIP("No frame reader on this platform");
}

void tst_LongshotPipeline::closureCandidatesPreferOverlapAndGap()
{
    SolveResult solve;
    // Frames 0..9 scrolled down 100 each, then 10..19 back up 100 each: frame 2 and frame 18 overlap.
    std::vector<qint64> times;
    for (int i = 0; i < 20; ++i) {
        solve.positions.push_back(i < 10 ? i * 100 : (9 - (i - 10)) * 100);
        times.push_back(i * 50);
    }
    const auto pairs = LongshotPipeline::selectClosureCandidates(solve, 480, times, 5, 64);
    QVERIFY(!pairs.empty());
    bool crossesLegs = false;
    for (const auto& [a, b] : pairs) {
        QVERIFY(b - a >= 5);                                   // not chain neighbours
        QVERIFY(qAbs(*solve.positions[a] - *solve.positions[b]) < 480); // overlapping viewports
        if (a < 10 && b >= 10) crossesLegs = true;             // a down-leg frame against an up-leg frame
    }
    QVERIFY(crossesLegs);
    // Unplaced frames (another island) pair with nearby placed ones so islands
    // can be rejoined; the time-gap rule does not apply to rejoin pairs, only
    // the already-tried chain neighbours (gap 1) are skipped.
    solve.positions[15] = std::nullopt;
    const auto withIsland = LongshotPipeline::selectClosureCandidates(solve, 480, times, 5, 64);
    int rejoinPairs = 0;
    for (const auto& [a, b] : withIsland) {
        if (a == 15 || b == 15) { ++rejoinPairs; QVERIFY(b - a >= 2); }
    }
    QVERIFY(rejoinPairs >= 2);
    // The frame budget bounds how many distinct frames are involved.
    const auto tight = LongshotPipeline::selectClosureCandidates(solve, 480, times, 5, 4);
    std::set<int> distinct;
    for (const auto& [a, b] : tight) { distinct.insert(a); distinct.insert(b); }
    QVERIFY(distinct.size() <= 4);
}

void tst_LongshotPipeline::masksResolvePerFrame()
{
    std::vector<FrameFeatures> frames(3);
    for (auto& f : frames) f.validContentRect = QRect(0, 0, 640, 480);
    ShiftObservation a;
    a.shift = {0, 1, 60, 0.9};
    a.bandsFrom = {40, 0, 0, 0};
    a.bandsTo = {40, 0, 0, 0};
    a.movingSpanFrom = a.movingSpanTo = QRect(0, 0, 540, 480);
    ShiftObservation b;
    b.shift = {1, 2, -60, 0.9};
    b.bandsFrom = {0, 0, 0, 0};
    b.bandsTo = {48, 0, 0, 0}; // scroll-up header on frame 2 only
    b.movingSpanFrom = b.movingSpanTo = QRect(0, 0, 540, 480);
    LongshotPipeline::resolveFrameMasks(frames, {a, b});
    QCOMPARE(frames[0].excludedBands.top, 40);
    QCOMPARE(frames[1].excludedBands.top, 40); // max over its observations
    QCOMPARE(frames[2].excludedBands.top, 48);
    QCOMPARE(frames[2].validContentRect, QRect(0, 48, 540, 432));
    QCOMPARE(frames[0].validContentRect, QRect(0, 40, 540, 440));
}

void tst_LongshotPipeline::trajectories_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("disturbance");
    QTest::newRow("constant") << 0 << 0;
    QTest::newRow("fling") << 1 << 0;
    QTest::newRow("back-and-forth") << 2 << 0;
    QTest::newRow("pauses") << 3 << 0;
    QTest::newRow("constant+sticky header") << 0 << 1;
    QTest::newRow("pauses+sticky header") << 3 << 1;
    QTest::newRow("back-and-forth+scroll-up header") << 2 << 2;
    QTest::newRow("constant+sidebar") << 0 << 3;
    QTest::newRow("constant+hover") << 0 << 4;
    QTest::newRow("constant+lazy load") << 0 << 5;
}

void tst_LongshotPipeline::trajectories()
{
    QFETCH(int, kind);
    QFETCH(int, disturbance);
    Trajectory t;
    switch (kind) {
    case 0: t = constantSpeed(60, 0, 45); break;
    case 1: t = fling(50, 200, 140); break;
    case 2: t = backAndForth(70, 900, 600, 50); break;
    default: t = withPauses(constantSpeed(40, 0, 45), 5, 3); break;
    }
    Disturbances d;
    switch (disturbance) {
    case 1: d.stickyHeaderHeight = 40; break;
    case 2: d.scrollUpHeaderHeight = 48; break;
    case 3: d.sidebarWidth = 100; break;
    case 4: d.hoverChange = true; break;
    case 5: d.lazyLoadRow = 1400; d.lazyLoadFrame = 20; break;
    default: break;
    }
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("traj-%1-%2.mp4").arg(kind).arg(disturbance)), PageSpec{}, t, d, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    int lastPercent = -1;
    bool monotonic = true;
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{},
                                                      [&lastPercent, &monotonic](int p) { monotonic = monotonic && p >= lastPercent; lastPercent = p; return true; });
    QCOMPARE(int(a.error), int(LongshotError::None));
    QCOMPARE(a.frames.size(), r.trajectory.offsets.size());
    QCOMPARE(a.thumbnails.size(), a.frames.size());
    QVERIFY(monotonic);
    QCOMPARE(lastPercent, 100);
    int placed = 0;
    const double accuracy = placedAccuracy(a, r.trajectory, &placed);
    QVERIFY2(placed >= int(kMinPlacedFraction * a.frames.size()),
             qPrintable(QStringLiteral("placed %1 of %2, breaks %3").arg(placed).arg(a.frames.size()).arg(a.solve.breakTimesMs.size())));
    QVERIFY2(accuracy >= kMinPlacedFraction, qPrintable(QStringLiteral("accuracy %1").arg(accuracy)));
    QVERIFY2(a.solve.breakTimesMs.empty(), qPrintable(QStringLiteral("breaks: %1").arg(a.solve.breakTimesMs.size())));
    if (kind == 2) QVERIFY(a.closuresAccepted > 0); // back-and-forth must produce loop closures
    if (disturbance == 1) {
        for (const FrameFeatures& f : a.frames) QVERIFY2(qAbs(f.excludedBands.top - 40) <= 2, qPrintable(QString::number(f.excludedBands.top)));
    }
    if (disturbance == 3) {
        for (const FrameFeatures& f : a.frames) QVERIFY(f.validContentRect.right() <= kViewport.width() - 100 + 2);
    }
}

void tst_LongshotPipeline::stationaryRecordingYieldsOneFrame()
{
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("still.mp4")), PageSpec{}, constantSpeed(30, 500, 0), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{}, {});
    QCOMPARE(int(a.error), int(LongshotError::None));
    QVERIFY(a.solve.breakTimesMs.empty());
    for (size_t i = 0; i < a.frames.size(); ++i) {
        QVERIFY(a.solve.positions[i].has_value());
        QCOMPARE(*a.solve.positions[i], 0);
        if (i > 0) QVERIFY(a.frames[i].stationary);
    }
}

void tst_LongshotPipeline::blankGapReportsBreak()
{
    PageSpec spec;
    spec.height = 6000;
    spec.blankTop = 2000;
    spec.blankHeight = 1400;
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("gap.mp4")), spec, constantSpeed(90, 800, 50), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{}, {});
    QCOMPARE(int(a.error), int(LongshotError::None));
    // The viewport is entirely blank for several frames: those pairs are
    // rejected and the two sides cannot be joined with confidence.
    QVERIFY2(!a.solve.breakTimesMs.empty(), "a blank gap taller than the viewport must be reported as a break");
    QVERIFY(a.rejectedPairs > 0);
    int placed = 0;
    QCOMPARE(placedAccuracy(a, r.trajectory, &placed), 1.0); // no false join across the gap
    QVERIFY(placed >= 20); // the larger side survives
}

void tst_LongshotPipeline::tooFewFramesFails()
{
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("short.mp4")), PageSpec{}, constantSpeed(10, 0, 40), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, 40, QRect())); // 40 ms at 20 fps = one frame
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, 40, QRect(), PipelineParams{}, {});
    QCOMPARE(int(a.error), int(LongshotError::TooFewFrames));
}

void tst_LongshotPipeline::cancelStopsEarly()
{
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("cancel.mp4")), PageSpec{}, constantSpeed(60, 0, 45), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    int calls = 0;
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{},
                                                      [&calls](int) { return ++calls < 3; });
    QCOMPARE(int(a.error), int(LongshotError::Cancelled));
    QVERIFY(a.frames.size() < 60);
}

void tst_LongshotPipeline::incrementalReusesKnownFrames()
{
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("incremental.mp4")), PageSpec{}, constantSpeed(40, 0, 45), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    const AnalysisResult full = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{}, {});
    QCOMPARE(int(full.error), int(LongshotError::None));
    QCOMPARE(full.frames.size(), size_t(40));
    const std::vector<FrameFeatures> known(full.frames.begin(), full.frames.begin() + 20);
    const std::vector<QImage> knownThumbs(full.thumbnails.begin(), full.thumbnails.begin() + 20);
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    int analyzed = -1;
    const AnalysisResult inc = LongshotPipeline::analyzeIncremental(*source, r.path, 0, -1, QRect(), PipelineParams{}, {}, known, knownThumbs, &analyzed);
    QCOMPARE(int(inc.error), int(LongshotError::None));
    QCOMPARE(analyzed, 20);
    QCOMPARE(inc.frames.size(), size_t(40));
    QCOMPARE(inc.thumbnails.size(), size_t(40));
    int placed = 0;
    QVERIFY(placedAccuracy(inc, r.trajectory, &placed) >= kMinPlacedFraction);
    QCOMPARE(placed, 40);
}

QTEST_MAIN(tst_LongshotPipeline)
#include "tst_LongshotPipeline.moc"
