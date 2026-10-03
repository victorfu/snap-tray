#include "IVideoEncoder.h"
#include "SyntheticScroll.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotRenderer.h"

#include <QtTest>
#include <limits>
#include <memory>
#include <QTemporaryDir>

using namespace SnapTray::Longshot;
using namespace SyntheticScroll;

namespace {

constexpr double kMaxDuplicatedFraction = 0.01;
constexpr double kMaxMissingFraction = 0.01;
constexpr double kMaxMisalignedFraction = 0.01;
constexpr double kMaxUnmatchedFraction = 0.03; // codec noise on dense text rows
constexpr int kMaxAbsoluteRows = 8;            // measured values are <= 2 rows
constexpr double kProfileTolerance = 6.0;      // same tolerance as the oracle's row matching
constexpr int kMaxSpuriousCrop = 4;            // side columns a recording without static panels may lose
constexpr int kMaxUnbalancedMissingRows = 2;   // lazy load with a majority of placeholder frames

// Serves in-memory frames at 50 ms intervals (no decoder involved).
class MemorySource : public LongshotFrameSource
{
public:
    explicit MemorySource(std::vector<QImage> frames) : m_frames(std::move(frames)) {}
    bool open(const QString&, qint64, qint64, const QRect&) override { m_next = 0; return !m_frames.empty(); }
    std::optional<QImage> next(qint64* tMs) override
    {
        if (m_next >= m_frames.size()) return std::nullopt;
        if (tMs) *tMs = qint64(m_next) * 50;
        return m_frames[m_next++];
    }
    QSize frameSize() const override { return m_frames.empty() ? QSize() : m_frames.front().size(); }
    QSize videoSize() const override { return frameSize(); }
    double frameRate() const override { return 20.0; }
    int expectedFrameCount() const override { return int(m_frames.size()); }
    QString lastError() const override { return {}; }

private:
    std::vector<QImage> m_frames;
    size_t m_next = 0;
};

// Decorator over the native source that stops delivering frames after a limit and
// reports an error, like a decoder failing mid-stream.
class FailingSource : public LongshotFrameSource
{
public:
    FailingSource(std::unique_ptr<LongshotFrameSource> inner, int limit) : m_inner(std::move(inner)), m_limit(limit) {}
    bool open(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop) override
    {
        m_delivered = 0;
        m_error.clear();
        return m_inner->open(path, startMs, endMs, crop);
    }
    std::optional<QImage> next(qint64* tMs) override
    {
        if (m_delivered >= m_limit) { m_error = QStringLiteral("injected decode failure"); return std::nullopt; }
        ++m_delivered;
        return m_inner->next(tMs);
    }
    QSize frameSize() const override { return m_inner->frameSize(); }
    QSize videoSize() const override { return m_inner->videoSize(); }
    double frameRate() const override { return m_inner->frameRate(); }
    int expectedFrameCount() const override { return m_inner->expectedFrameCount(); }
    QString lastError() const override { return m_error.isEmpty() ? m_inner->lastError() : m_error; }

private:
    std::unique_ptr<LongshotFrameSource> m_inner;
    int m_limit;
    int m_delivered = 0;
    QString m_error;
};

struct Run {
    QImage page;
    Trajectory trajectory;
    AnalysisResult analysis;
    RenderResult render;
    QString path;
};

Run runEndToEnd(const QString& path, const PageSpec& spec, const Trajectory& base, const Disturbances& d,
                const LongshotOptions& options, QString* error)
{
    Run run;
    run.page = renderPage(spec);
    run.trajectory = clampTrajectory(base, run.page.height(), kViewport.height());
    std::vector<QImage> frames;
    for (size_t i = 0; i < run.trajectory.offsets.size(); ++i) frames.push_back(renderFrame(run.page, kViewport, run.trajectory, int(i), d));
    *error = encodeFrames(path, frames, kFrameRate);
    if (!error->isEmpty()) return run;
    run.path = path;
    auto source = FrameReaderLongshotSource::createNative();
    if (!source->open(path, 0, -1, QRect())) { *error = source->lastError(); return run; }
    run.analysis = LongshotPipeline::analyze(*source, path, 0, -1, QRect(), PipelineParams{}, {});
    run.render = LongshotRenderer::render(*source, path, 0, -1, QRect(), run.analysis, options, {});
    return run;
}

void checkAgainstTruth(const Run& run, int headerHeight = 0, RowMatchReport* report = nullptr)
{
    QCOMPARE(int(run.render.error), int(LongshotError::None));
    QCOMPARE(run.render.parts.size(), 1);
    const QImage out = run.render.parts.first();
    const int first = *std::min_element(run.trajectory.offsets.begin(), run.trajectory.offsets.end()) + headerHeight;
    const int last = *std::max_element(run.trajectory.offsets.begin(), run.trajectory.offsets.end()) + kViewport.height() - 1;
    QImage truth = run.page;
    if (run.render.autoCroppedLeft || run.render.autoCroppedRight) {
        truth = truth.copy(run.render.autoCroppedLeft, 0, truth.width() - run.render.autoCroppedLeft - run.render.autoCroppedRight, truth.height());
    }
    const RowMatchReport r = compareWithGroundTruth(out, truth, first, last);
    if (report) *report = r;
    qInfo() << "METRICS" << r.outputRows << r.matchedRows << r.duplicatedRows << r.missingRows << r.misalignedRows << r.unmatchedRows << "first" << first << "last" << last;
    const double rows = r.outputRows;
    QVERIFY2(r.duplicatedRows <= kMaxDuplicatedFraction * rows, qPrintable(QStringLiteral("duplicated %1 of %2").arg(r.duplicatedRows).arg(rows)));
    QVERIFY2(r.missingRows <= kMaxMissingFraction * (last - first + 1), qPrintable(QStringLiteral("missing %1").arg(r.missingRows)));
    QVERIFY2(r.misalignedRows <= kMaxMisalignedFraction * rows, qPrintable(QStringLiteral("misaligned %1").arg(r.misalignedRows)));
    QVERIFY2(r.unmatchedRows <= kMaxUnmatchedFraction * rows, qPrintable(QStringLiteral("unmatched %1").arg(r.unmatchedRows)));
    QVERIFY(run.render.breakRows.empty());
    QVERIFY2(r.duplicatedRows <= kMaxAbsoluteRows && r.missingRows <= kMaxAbsoluteRows && r.misalignedRows <= kMaxAbsoluteRows
                 && r.unmatchedRows <= kMaxAbsoluteRows,
             qPrintable(QStringLiteral("absolute: dup %1 missing %2 misaligned %3 unmatched %4")
                            .arg(r.duplicatedRows).arg(r.missingRows).arg(r.misalignedRows).arg(r.unmatchedRows)));
}

} // namespace

class tst_LongshotRenderer : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void tileAssignmentPrefersStationaryAndCentred();
    void seamsMoveToLowGradientRows();
    void endToEnd_data();
    void endToEnd();
    void renderStationary();
    void stickyHeaderOnceWhenRequested();
    void sidebarIsAutoCropped();
    void heightCapSplitsOrReports();
    void breakLeavesGapReported();
    void decodeFailureIsReported();
    void lowConfidenceRowsFollowWeakEdges();

private:
    QTemporaryDir m_dir;
};

void tst_LongshotRenderer::initTestCase()
{
    QVERIFY(m_dir.isValid());
    if (!std::unique_ptr<IVideoEncoder>(IVideoEncoder::createNativeEncoder())) QSKIP("No native encoder on this platform");
    if (!FrameReaderLongshotSource::createNative()) QSKIP("No frame reader on this platform");
}

void tst_LongshotRenderer::tileAssignmentPrefersStationaryAndCentred()
{
    AnalysisResult a;
    a.frameSize = QSize(640, 480);
    a.frames.resize(3);
    for (auto& f : a.frames) f.validContentRect = QRect(0, 0, 640, 480);
    a.frames[1].stationary = true;
    a.solve.positions = {0, 100, 200};
    LongshotOptions options;
    options.tileRows = 64;
    const auto tiles = LongshotRenderer::assignTiles(a, options, 680, 0);
    QCOMPARE(int(tiles.size()), 11); // ceil(680 / 64)
    // Output rows 100..163 are covered by frames 0 (rows 100-163 of it), 1 (0-63) and 2? no: frame 2 starts at 200.
    // Frame 1 is stationary and the rows sit near its top; frame 0 has them mid-frame. Stationary wins.
    QCOMPARE(tiles[1].outputTop, 64);
    const TileAssignment& t = tiles[2]; // rows 128..191: frames 0 and 1 both cover
    QCOMPARE(t.frameIndex, 1);
    // Rows 640..679 are only covered by frame 2.
    QCOMPARE(tiles.back().frameIndex, 2);
    // No frame covers a tile beyond the content: a 700-row request leaves the last tile unassigned.
    const auto tall = LongshotRenderer::assignTiles(a, options, 760, 0);
    QCOMPARE(tall.back().frameIndex, -1);
}

void tst_LongshotRenderer::seamsMoveToLowGradientRows()
{
    AnalysisResult a;
    a.frameSize = QSize(640, 480);
    a.frames.resize(2);
    for (auto& f : a.frames) {
        f.validContentRect = QRect(0, 0, 640, 480);
        f.rowGradient.assign(480, 10.0f);
        f.rowMean.assign(480, 100.0f);
    }
    a.solve.positions = {0, 32};
    // Frame 0 has a flat (gap) row at output row 70; the tile boundary at 64 should move there.
    a.frames[0].rowGradient[70] = 0.0f;
    std::vector<TileAssignment> tiles = {{0, 64, 0}, {64, 128, 1}};
    LongshotRenderer::placeSeams(tiles, a, 0);
    QCOMPARE(tiles[0].outputBottom, 70);
    QCOMPARE(tiles[1].outputTop, 70);
}

void tst_LongshotRenderer::endToEnd_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("constant") << 0;
    QTest::newRow("fling") << 1;
    QTest::newRow("back-and-forth") << 2;
    QTest::newRow("pauses") << 3;
    QTest::newRow("hover") << 4;
    QTest::newRow("lazy load") << 5;
    QTest::newRow("lazy load unbalanced") << 6;
}

void tst_LongshotRenderer::endToEnd()
{
    QFETCH(int, kind);
    Trajectory t;
    Disturbances d;
    switch (kind) {
    case 0: t = constantSpeed(60, 0, 45); break;
    case 1: t = fling(50, 200, 140); break;
    case 2: t = backAndForth(70, 900, 600, 50); break;
    case 3: t = withPauses(constantSpeed(40, 0, 45), 5, 3); break;
    case 4: t = constantSpeed(60, 0, 45); d.hoverChange = true; break;
    case 5: t = constantSpeed(60, 0, 45); d.lazyLoadRow = 1400; d.lazyLoadFrame = 20; break;
    // Row 1400 is visible in frames ~21-31; frames 28-31 show it loaded, 21-27 the
    // placeholder. The block's top edge covers the last 8 rows of the 1344-1407
    // tile, exactly the agreement allowance: strict agreement counts that as a
    // disagreement. (Rows 1412-1430 would move the boundary deeper into the next
    // tile but trigger a wrong chain match at the placeholder->loaded pair; see
    // the final-fix report.)
    default: t = constantSpeed(60, 0, 45); d.lazyLoadRow = 1400; d.lazyLoadFrame = 28; break;
    }
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("e2e-%1.mp4").arg(kind)), PageSpec{}, t, d, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    RowMatchReport report;
    checkAgainstTruth(run, 0, &report);
    QCOMPARE(run.render.fullHeightPx, run.render.parts.first().height());
    QVERIFY(!run.render.stickyHeaderIncluded);
    // No static side panel: the side crop may only lose codec-noise columns.
    QVERIFY2(run.render.autoCroppedLeft + run.render.autoCroppedRight <= kMaxSpuriousCrop,
             qPrintable(QStringLiteral("cropped %1 + %2").arg(run.render.autoCroppedLeft).arg(run.render.autoCroppedRight)));
    if (kind == 6) QVERIFY2(report.missingRows <= kMaxUnbalancedMissingRows, qPrintable(QString::number(report.missingRows)));
}

void tst_LongshotRenderer::renderStationary()
{
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("still.mp4")), PageSpec{}, constantSpeed(30, 500, 0), Disturbances{}, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(int(run.render.error), int(LongshotError::None));
    QCOMPARE(run.render.parts.first().size(), kViewport);
    QVERIFY(run.render.breakRows.empty());
    QVERIFY(run.render.autoCroppedLeft + run.render.autoCroppedRight <= kMaxSpuriousCrop);
}

void tst_LongshotRenderer::stickyHeaderOnceWhenRequested()
{
    Disturbances d;
    d.stickyHeaderHeight = 40;
    QString error;
    // Default: header excluded entirely; the content below it is stitched.
    const Run without = runEndToEnd(m_dir.filePath(QStringLiteral("header-off.mp4")), PageSpec{}, constantSpeed(60, 0, 45), d, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    checkAgainstTruth(without, 40);
    QVERIFY(!without.render.stickyHeaderIncluded);
    QVERIFY2(without.render.autoCroppedLeft + without.render.autoCroppedRight <= kMaxSpuriousCrop,
             qPrintable(QStringLiteral("cropped %1 + %2").arg(without.render.autoCroppedLeft).arg(without.render.autoCroppedRight)));
    // Requested: header appears exactly once at the top, output is taller by the band.
    LongshotOptions options;
    options.includeStickyHeader = true;
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(without.path, 0, -1, QRect()));
    const RenderResult with = LongshotRenderer::render(*source, without.path, 0, -1, QRect(), without.analysis, options, {});
    QVERIFY(with.stickyHeaderIncluded);
    QCOMPARE(with.parts.first().height(), without.render.parts.first().height() + 40);
    // The header rows do not recur further down: compare the header band's row profiles
    // against every 40-row window below it.
    const QImage out = with.parts.first();
    int repeats = 0;
    for (int y = 40; y + 40 <= out.height(); ++y) {
        bool same = true;
        for (int r = 0; r < 40 && same; ++r) same = rowProfileDistance(out, r, out, y + r) < kProfileTolerance;
        if (same) ++repeats;
    }
    QCOMPARE(repeats, 0);
}

void tst_LongshotRenderer::sidebarIsAutoCropped()
{
    Disturbances d;
    d.sidebarWidth = 100;
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("sidebar.mp4")), PageSpec{}, constantSpeed(60, 0, 45), d, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(run.render.autoCroppedRight >= 96 && run.render.autoCroppedRight <= 104, qPrintable(QString::number(run.render.autoCroppedRight)));
    QCOMPARE(run.render.parts.first().width(), kViewport.width() - run.render.autoCroppedLeft - run.render.autoCroppedRight);
    checkAgainstTruth(run);
}

void tst_LongshotRenderer::heightCapSplitsOrReports()
{
    QString error;
    LongshotOptions capped;
    capped.maxHeightPx = 1000;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("cap.mp4")), PageSpec{}, constantSpeed(60, 0, 45), Disturbances{}, capped, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(run.render.fullHeightPx > 1000);
    QVERIFY(run.render.heightCapped);
    QCOMPARE(run.render.parts.size(), 1);
    QCOMPARE(run.render.parts.first().height(), 1000);
    LongshotOptions split = capped;
    split.splitOversize = true;
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(run.path, 0, -1, QRect()));
    const RenderResult parts = LongshotRenderer::render(*source, run.path, 0, -1, QRect(), run.analysis, split, {});
    QVERIFY(!parts.heightCapped);
    QCOMPARE(parts.parts.size(), (run.render.fullHeightPx + 999) / 1000);
    int total = 0;
    for (const QImage& p : parts.parts) { QVERIFY(p.height() <= 1000); total += p.height(); }
    QCOMPARE(total, run.render.fullHeightPx);
}

void tst_LongshotRenderer::breakLeavesGapReported()
{
    PageSpec spec;
    spec.height = 6000;
    spec.blankTop = 2000;
    spec.blankHeight = 1400;
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("gap.mp4")), spec, constantSpeed(90, 800, 50), Disturbances{}, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(int(run.render.error), int(LongshotError::None));
    // Only the kept island is rendered; the analysis break is passed through so
    // the UI can mark where the recording could not be joined.
    QVERIFY(!run.analysis.solve.breakTimesMs.empty());
    QVERIFY(run.render.parts.first().height() < 6000);
    QVERIFY(run.render.breakRows.empty());
    int minPos = std::numeric_limits<int>::max();
    int maxPos = std::numeric_limits<int>::min();
    int validHeight = 0;
    qint64 lastPlacedMs = -1;
    qint64 firstPlacedMs = std::numeric_limits<qint64>::max();
    for (size_t i = 0; i < run.analysis.frames.size(); ++i) {
        if (!run.analysis.solve.positions[i].has_value()) continue;
        minPos = std::min(minPos, *run.analysis.solve.positions[i]);
        if (*run.analysis.solve.positions[i] >= maxPos) { maxPos = *run.analysis.solve.positions[i]; validHeight = run.analysis.frames[i].validContentRect.height(); }
        lastPlacedMs = std::max(lastPlacedMs, run.analysis.frames[i].tMs);
        firstPlacedMs = std::min(firstPlacedMs, run.analysis.frames[i].tMs);
    }
    QVERIFY(std::abs(run.render.fullHeightPx - (maxPos - minPos + validHeight)) <= kTileRows);
    // Every dropped island starts outside the kept island's time span (here the first island is the one dropped).
    for (qint64 breakMs : run.analysis.solve.breakTimesMs) QVERIFY2(breakMs < firstPlacedMs || breakMs > lastPlacedMs, qPrintable(QString::number(breakMs)));
}

void tst_LongshotRenderer::decodeFailureIsReported()
{
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("fail.mp4")), PageSpec{}, constantSpeed(20, 0, 45), Disturbances{}, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(int(run.render.error), int(LongshotError::None));
    FailingSource failing(FrameReaderLongshotSource::createNative(), 10);
    const RenderResult result = LongshotRenderer::render(failing, run.path, 0, -1, QRect(), run.analysis, LongshotOptions{}, {});
    QCOMPARE(int(result.error), int(LongshotError::SourceUnavailable));
    QVERIFY(result.parts.isEmpty());
}

void tst_LongshotRenderer::lowConfidenceRowsFollowWeakEdges()
{
    // Two 128x128 frames, frame 1 placed 64 rows below frame 0 by one edge.
    const QSize size(128, 128);
    std::vector<QImage> images;
    for (int i = 0; i < 2; ++i) {
        QImage image(size, QImage::Format_RGB32);
        image.fill(QColor(40 * (i + 1), 90, 160));
        images.push_back(image);
    }
    AnalysisResult a;
    a.frameSize = size;
    a.frames.resize(2);
    for (int i = 0; i < 2; ++i) {
        a.frames[i].tMs = i * 50;
        a.frames[i].validContentRect = QRect(QPoint(0, 0), size);
    }
    a.solve.positions = {0, 64};
    auto renderWith = [&](double confidence) {
        a.edges = {PairShift{0, 1, 64, confidence}};
        MemorySource source(images);
        return LongshotRenderer::render(source, QString(), 0, -1, QRect(), a, LongshotOptions{}, {});
    };
    // 0.75 is accepted by the analyzer (>= 0.70) but weak: frame 1's rows are marked.
    const RenderResult weak = renderWith(0.75);
    QCOMPARE(int(weak.error), int(LongshotError::None));
    QCOMPARE(weak.parts.first().height(), 192);
    QVERIFY(!weak.lowConfidenceRows.empty());
    const auto has = [&](int row) { return std::find(weak.lowConfidenceRows.begin(), weak.lowConfidenceRows.end(), row) != weak.lowConfidenceRows.end(); };
    QVERIFY(has(150));  // painted from frame 1
    QVERIFY(!has(10));  // frame 0 is the anchor, never marked
    const RenderResult strong = renderWith(0.95);
    QCOMPARE(int(strong.error), int(LongshotError::None));
    QVERIFY(strong.lowConfidenceRows.empty());
    // A source whose frames do not match the analysed size is refused.
    std::vector<QImage> wrongSize;
    for (const QImage& image : images) wrongSize.push_back(image.copy(0, 0, 96, 128));
    MemorySource mismatched(wrongSize);
    QCOMPARE(int(LongshotRenderer::render(mismatched, QString(), 0, -1, QRect(), a, LongshotOptions{}, {}).error),
             int(LongshotError::SourceUnavailable));
}

QTEST_MAIN(tst_LongshotRenderer)
#include "tst_LongshotRenderer.moc"
