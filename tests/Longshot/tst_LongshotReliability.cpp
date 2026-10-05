#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotRenderer.h"
#include "longshot/LongshotSession.h"
#include "longshot/PositionSolver.h"

#include <QtTest>
#include <QPainter>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <algorithm>

using namespace SnapTray::Longshot;

namespace {
constexpr qint64 kFrameIntervalMs = 50;

class MemorySource final : public LongshotFrameSource
{
public:
    explicit MemorySource(std::vector<QImage> frames) : m_frames(std::move(frames)) {}
    bool open(const QString&, qint64 startMs, qint64 endMs, const QRect&) override
    {
        m_start = size_t((startMs + kFrameIntervalMs - 1) / kFrameIntervalMs);
        m_next = m_start;
        m_end = endMs < 0 ? m_frames.size()
                         : std::min(m_frames.size(), size_t((endMs + kFrameIntervalMs - 1) / kFrameIntervalMs));
        return true;
    }
    std::optional<QImage> next(qint64* tMs) override
    {
        if (m_next >= m_end) return std::nullopt;
        if (tMs) *tMs = qint64(m_next) * kFrameIntervalMs;
        return m_frames[m_next++];
    }
    QSize frameSize() const override { return m_frames.front().size(); }
    QSize videoSize() const override { return frameSize(); }
    double frameRate() const override { return 20.0; }
    int expectedFrameCount() const override { return int(m_end - m_start); }
    QString lastError() const override { return {}; }
private:
    std::vector<QImage> m_frames;
    size_t m_next = 0;
    size_t m_start = 0;
    size_t m_end = 0;
};

QImage texture()
{
    QImage page(128, 512, QImage::Format_RGB32);
    QRandomGenerator random(42);
    for (int y = 0; y < page.height(); ++y) {
        for (int x = 0; x < page.width(); ++x) {
            const int value = random.bounded(256);
            page.setPixel(x, y, qRgb(value, value, value));
        }
    }
    return page;
}
} // namespace

class tst_LongshotReliability : public QObject
{
    Q_OBJECT
private slots:
    void weakJoinRemainsMarkedAfterPause();
    void unmatchedFramesAreRejected_data();
    void unmatchedFramesAreRejected();
    void texturedStationaryFramesRemainValid();
    void noisyStationaryFramesAreDetected();
    void localizedPauseNoiseDoesNotMaskAllContent_data();
    void localizedPauseNoiseDoesNotMaskAllContent();
    void staticSurroundingsDoNotHideSmallMovingContent();
    void failedSessionDoesNotCacheSuccess();
    void cancelledTrimBeforeRenderInvalidatesPreviousImage();
    void sparseSlowScrollIsNotStationary();
    void changingSidebar_data();
    void changingSidebar();
    void complementaryMasksCoverTheWholeOutput();
    void isolatedCodecRowsDoNotCreateBands_data();
    void isolatedCodecRowsDoNotCreateBands();
    void texturedOverlaysStillCreateBands_data();
    void texturedOverlaysStillCreateBands();
    void restoredHeaderRespectsHorizontalMask();
    void sparseScrollingColumnsAreNotSidebars_data();
    void sparseScrollingColumnsAreNotSidebars();
};

void tst_LongshotReliability::unmatchedFramesAreRejected_data()
{
    QTest::addColumn<bool>("blank");
    QTest::newRow("blank") << true;
    QTest::newRow("unrelated-content") << false;
}

void tst_LongshotReliability::unmatchedFramesAreRejected()
{
    QFETCH(bool, blank);
    const QImage page = texture();
    QImage first = page.copy(0, 0, 128, 128);
    QImage second = page.copy(0, 256, 128, 128);
    if (blank) { first.fill(Qt::white); second.fill(Qt::white); }
    MemorySource source({first, second});
    QVERIFY(source.open({}, 0, -1, {}));
    const auto analysis = LongshotPipeline::analyze(source, {}, 0, -1, {}, {}, {});
    QVERIFY(analysis.edges.empty());
    QCOMPARE(analysis.error, LongshotError::NoReliableContent);
    const auto rendered = LongshotRenderer::render(source, {}, 0, -1, {}, analysis, {}, {});
    QCOMPARE(rendered.error, LongshotError::NoReliableContent);
    QVERIFY(rendered.parts.empty());
}

void tst_LongshotReliability::localizedPauseNoiseDoesNotMaskAllContent_data()
{
    QTest::addColumn<int>("height");
    QTest::addColumn<bool>("blankMargins");
    QTest::newRow("minimum-crop") << 64 << false;
    QTest::newRow("medium-crop") << 128 << false;
    QTest::newRow("full-frame") << 480 << false;
    QTest::newRow("blank-margins") << 480 << true;
}
void tst_LongshotReliability::localizedPauseNoiseDoesNotMaskAllContent()
{
    QFETCH(int,height); QFETCH(bool,blankMargins);
    auto first=texture().copy(0,0,128,height);
    if (blankMargins) {
        first.fill(QColor(12,12,12));
        QPainter painter(&first);
        painter.drawImage(0,height/2-64,texture().copy(0,0,128,128));
    }
    QImage second=first.copy();
    for(int y=height/2;y<height/2+12;++y) for(int x=0;x<second.width();++x) {
        const int gray=std::clamp(qGray(first.pixel(x,y))+(x%2 ? 10 : -10),0,255);
        second.setPixel(x,y,qRgb(gray,gray,gray));
    }
    const auto from=LongshotAnalyzer::computeFeatures(first,0), to=LongshotAnalyzer::computeFeatures(second,50);
    const auto observation=LongshotAnalyzer::estimateShift(first,from,second,to,0,1,{});
    QVERIFY(observation.has_value()); QVERIFY(observation->stationary);
    QCOMPARE(observation->shift.dy,0);
    QCOMPARE(observation->bandsFrom,StaticBands{});
    QCOMPARE(observation->bandsTo,StaticBands{});
    AnalyzerParams strict;
    strict.minPeakScore=1.0;
    QVERIFY(!LongshotAnalyzer::estimateShift(first,from,second,to,0,1,strict));
}
void tst_LongshotReliability::staticSurroundingsDoNotHideSmallMovingContent()
{
    const auto page=texture();
    const auto first=page.copy(0,0,128,480);
    QImage second=first.copy();
    for(int y=240;y<256;++y) for(int x=0;x<second.width();++x)
        second.setPixel(x,y,page.pixel(x,y+1));
    const auto from=LongshotAnalyzer::computeFeatures(first,0), to=LongshotAnalyzer::computeFeatures(second,50);
    const auto observation=LongshotAnalyzer::estimateShift(first,from,second,to,0,1,{});
    QVERIFY(!observation || !observation->stationary);
}

void tst_LongshotReliability::texturedStationaryFramesRemainValid()
{
    const QImage frame = texture().copy(0, 0, 128, 128);
    MemorySource source({frame, frame});
    QVERIFY(source.open({}, 0, -1, {}));
    const auto analysis = LongshotPipeline::analyze(source, {}, 0, -1, {}, {}, {});
    QCOMPARE(analysis.error, LongshotError::None);
    QVERIFY(!analysis.edges.empty());
    const auto rendered = LongshotRenderer::render(source, {}, 0, -1, {}, analysis, {}, {});
    QCOMPARE(rendered.error, LongshotError::None);
    QCOMPARE(rendered.parts.size(), 1);
    QCOMPARE(rendered.parts.front(), frame);
}

void tst_LongshotReliability::noisyStationaryFramesAreDetected()
{
    const QImage from = texture().copy(0, 0, 128, 128);
    QImage to = from;
    for (int y = 0; y < to.height(); ++y) {
        for (int x = 0; x < to.width(); ++x) {
            const int value = std::min(255, qRed(to.pixel(x, y)) + 2);
            to.setPixel(x, y, qRgb(value, value, value));
        }
    }
    const auto observation = LongshotAnalyzer::estimateShift(
        from, LongshotAnalyzer::computeFeatures(from, 0),
        to, LongshotAnalyzer::computeFeatures(to, kFrameIntervalMs), 0, 1, {});
    QVERIFY(observation.has_value());
    QCOMPARE(observation->shift.dy, 0);
    QVERIFY(observation->stationary);
}

void tst_LongshotReliability::failedSessionDoesNotCacheSuccess()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QImage blank(128, 128, QImage::Format_RGB32);
    blank.fill(Qt::white);
    LongshotSession session([blank] {
        return std::make_unique<MemorySource>(std::vector<QImage>{blank, blank});
    });
    session.setRecording(file.fileName());
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto report = session.run({});
        QCOMPARE(report.error, LongshotError::NoReliableContent);
        QVERIFY(report.render.parts.empty());
        QVERIFY(!report.reusedRender);
    }
}

void tst_LongshotReliability::cancelledTrimBeforeRenderInvalidatesPreviousImage()
{
    const QImage page = texture();
    std::vector<QImage> frames;
    for (int i = 0; i < 6; ++i) frames.push_back(page.copy(0, i * 20, 128, 128));
    const auto factory = [frames] { return std::make_unique<MemorySource>(frames); };
    LongshotSession session(factory);
    session.setTrim(0, 150);
    const auto first = session.run({});
    QCOMPARE(first.error, LongshotError::None);
    QCOMPARE(first.render.fullHeightPx, 168);

    session.setTrim(0, 300);
    WorkStage stage = WorkStage::Analyze;
    const auto cancelled = session.run([&](int) { return stage != WorkStage::Render; },
                                       [&](WorkStage next) { stage = next; });
    QCOMPARE(cancelled.error, LongshotError::Cancelled);
    QCOMPARE(stage, WorkStage::Render);

    const auto retry = session.run({});
    QCOMPARE(retry.error, LongshotError::None);
    QVERIFY(retry.reusedSolve);
    QVERIFY(!retry.reusedRender);
    LongshotSession fresh(factory);
    fresh.setTrim(0, 300);
    const auto expected = fresh.run({});
    QCOMPARE(expected.error, LongshotError::None);
    QCOMPARE(expected.render.fullHeightPx, 228);
    QCOMPARE(retry.render.parts, expected.render.parts);
    QVERIFY(session.run({}).reusedRender);
}

void tst_LongshotReliability::sparseSlowScrollIsNotStationary()
{
    QImage page(640, 600, QImage::Format_RGB32);
    page.fill(Qt::white);
    {
        QPainter painter(&page);
        for (int y = 0, row = 0; y < page.height(); y += 30, ++row)
            painter.fillRect(24 + (row * 7) % 80, y, 50, 12, QColor(20, 20, 20));
    }
    std::vector<QImage> frames;
    constexpr int frameCount = 12;
    for (int i = 0; i < frameCount; ++i) frames.push_back(page.copy(0, i, 640, 480));
    for (int i = 1; i < frameCount; ++i) {
        const auto from = LongshotAnalyzer::computeFeatures(frames[i - 1], (i - 1) * 50);
        const auto to = LongshotAnalyzer::computeFeatures(frames[i], i * 50);
        const auto observation = LongshotAnalyzer::estimateShift(frames[i - 1], from, frames[i], to, i - 1, i, {});
        // An ambiguous sparse pair may be refused, but must never assert no motion.
        if (observation) {
            QVERIFY(!observation->stationary);
            QCOMPARE(observation->shift.dy, 1);
        }
    }
    MemorySource source(frames);
    QVERIFY(source.open({}, 0, -1, {}));
    const auto analysis = LongshotPipeline::analyze(source, {}, 0, -1, {}, {}, {});
    if (analysis.error == LongshotError::NoReliableContent) return;
    QCOMPARE(analysis.error, LongshotError::None);
    const auto rendered = LongshotRenderer::render(source, {}, 0, -1, {}, analysis, {}, {});
    QCOMPARE(rendered.error, LongshotError::None);
    // A shorter result must explicitly report the unjoined content.
    QVERIFY(rendered.fullHeightPx == 480 + frameCount - 1
            || !analysis.solve.breakTimesMs.empty() || !rendered.breakRows.empty()
            || !rendered.lowConfidenceRows.empty());
}

void tst_LongshotReliability::changingSidebar_data()
{
    QTest::addColumn<bool>("appears");
    QTest::addColumn<bool>("left");
    QTest::addColumn<bool>("split");
    for (bool appears : {false, true}) {
        for (bool left : {false, true}) {
            for (bool split : {false, true}) {
                const QByteArray label = QString("%1-%2-%3").arg(appears ? "appears" : "disappears")
                    .arg(left ? "left" : "right").arg(split ? "split" : "single").toLatin1();
                QTest::newRow(label.constData()) << appears << left << split;
            }
        }
    }
}

void tst_LongshotReliability::changingSidebar()
{
    QFETCH(bool, appears);
    QFETCH(bool, left);
    QFETCH(bool, split);
    const QImage page = texture();
    std::vector<QImage> frames;
    for (int i = 0; i < 3; ++i) {
        QImage frame = page.copy(0, i * 32, 128, 128);
        if (appears ? i > 0 : i < 2) {
            for (int y = 0; y < 128; ++y) {
                for (int x = left ? 0 : 96; x < (left ? 32 : 128); ++x) {
                    frame.setPixel(x, y, qRgb(y % 2 ? 0 : 255, 0, 0));
                }
            }
        }
        frames.push_back(frame);
    }
    MemorySource source(frames);
    QVERIFY(source.open({}, 0, -1, {}));
    const auto analysis = LongshotPipeline::analyze(source, {}, 0, -1, {}, {}, {});
    QCOMPARE(analysis.error, LongshotError::None);
    for (int i = 0; i < 3; ++i) QCOMPARE(analysis.solve.positions[i], std::optional<int>(i * 32));
    LongshotOptions options;
    options.splitOversize = split;
    options.maxHeightPx = split ? 80 : 30000;
    const auto result = LongshotRenderer::render(source, {}, 0, -1, {}, analysis, options, {});
    QCOMPARE(result.error, LongshotError::None);
    QCOMPARE(result.fullHeightPx, 192);
    QCOMPARE(result.autoCroppedLeft, 0);
    QCOMPARE(result.autoCroppedRight, 0);
    std::vector<int> expectedBreaks;
    int outputY = 0;
    for (const QImage& part : result.parts) {
        QCOMPARE(part.width(), 128);
        for (int y = 0; y < part.height(); ++y, ++outputY) {
            bool complete = true;
            for (int x = 0; x < part.width(); ++x) {
                bool covered = false;
                for (int i = 0; i < 3; ++i) {
                    covered = covered || analysis.frames[i].validContentRect.contains(x, outputY - i * 32);
                }
                const QRgb expected = covered ? page.pixel(x, outputY) : qRgb(255, 255, 255);
                QCOMPARE(part.pixel(x, y), expected);
                complete = complete && covered;
            }
            if (!complete) expectedBreaks.push_back(outputY);
        }
    }
    QCOMPARE(outputY, 192);
    QVERIFY(!expectedBreaks.empty());
    QCOMPARE(result.breakRows, expectedBreaks);
}

void tst_LongshotReliability::complementaryMasksCoverTheWholeOutput()
{
    const QImage truth = texture().copy(0, 0, 128, 128);
    QImage first = truth;
    QImage second = truth;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            if (x >= 64) first.setPixel(x, y, qRgb(255, 0, 0));
            else second.setPixel(x, y, qRgb(0, 0, 255));
        }
    }
    MemorySource source({first, second});
    AnalysisResult analysis;
    analysis.frameSize = truth.size();
    analysis.frames.resize(2);
    analysis.frames[0].validContentRect = QRect(0, 0, 64, 128);
    analysis.frames[1].validContentRect = QRect(64, 0, 64, 128);
    analysis.solve.positions = {0, 0};
    const auto result = LongshotRenderer::render(source, {}, 0, -1, {}, analysis, {}, {});
    QCOMPARE(result.error, LongshotError::None);
    QCOMPARE(result.parts.size(), 1);
    QCOMPARE(result.parts.front(), truth);
    QVERIFY(result.breakRows.empty());
}

void tst_LongshotReliability::isolatedCodecRowsDoNotCreateBands_data()
{
    QTest::addColumn<bool>("bottom");
    QTest::addColumn<int>("noiseRows");
    QTest::newRow("top-one-row") << false << 1;
    QTest::newRow("top-two-rows") << false << 2;
    QTest::newRow("bottom-one-row") << true << 1;
    QTest::newRow("bottom-two-rows") << true << 2;
}

void tst_LongshotReliability::isolatedCodecRowsDoNotCreateBands()
{
    QFETCH(bool, bottom);
    QFETCH(int, noiseRows);
    QImage page = texture();
    const int start = bottom ? 90 : 0;
    const int end = bottom ? 140 : 40;
    for (int y = start; y < end; ++y) {
        for (int x = 0; x < 128; ++x) page.setPixel(x, y, qRgb(100, 100, 100));
    }
    QImage from = page.copy(0, 0, 128, 128);
    QImage to = page.copy(0, 1, 128, 128);
    // Codec reconstruction can keep an isolated row similar at the same
    // screen coordinate even though its true, shifted neighbour differs.
    for (int i = 0; i < noiseRows; ++i) {
        const int y = (bottom ? 100 : 12) + i * 4;
        for (int x = 0; x < 128; ++x) {
            from.setPixel(x, y, qRgb(108, 108, 108));
            to.setPixel(x, y, qRgb(108, 108, 108));
        }
    }
    QCOMPARE(LongshotAnalyzer::detectStaticBands(from, to, 1, {}), StaticBands{});
}

void tst_LongshotReliability::texturedOverlaysStillCreateBands_data()
{
    QTest::addColumn<bool>("bottom");
    QTest::newRow("header") << false;
    QTest::newRow("footer") << true;
}

void tst_LongshotReliability::texturedOverlaysStillCreateBands()
{
    QFETCH(bool, bottom);
    const QImage page = texture();
    QImage from = page.copy(0, 0, 128, 128);
    QImage to = page.copy(0, 1, 128, 128);
    const int first = bottom ? 96 : 0;
    for (int y = first; y < first + 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            const int value = y % 2 ? 0 : 255;
            from.setPixel(x, y, qRgb(value, value, value));
            to.setPixel(x, y, qRgb(value, value, value));
        }
    }
    const StaticBands bands = LongshotAnalyzer::detectStaticBands(from, to, 1, {});
    QCOMPARE(bottom ? bands.bottom : bands.top, 32);
    QCOMPARE(bottom ? bands.top : bands.bottom, 0);
}

void tst_LongshotReliability::restoredHeaderRespectsHorizontalMask()
{
    const QImage truth = texture().copy(0, 0, 128, 128);
    QImage frame = truth;
    for (int y = 0; y < 128; ++y) {
        for (int x = 64; x < 128; ++x) frame.setPixel(x, y, qRgb(255, 0, 0));
    }
    MemorySource source({frame, truth});
    AnalysisResult analysis;
    analysis.frameSize = truth.size();
    analysis.frames.resize(2);
    analysis.frames[0].validContentRect = QRect(0, 16, 64, 112);
    analysis.frames[0].excludedBands.top = 16;
    analysis.frames[1].validContentRect = QRect(64, 16, 64, 112);
    analysis.solve.positions = {0, 0};
    LongshotOptions options;
    options.includeStickyHeader = true;
    const auto result = LongshotRenderer::render(source, {}, 0, -1, {}, analysis, options, {});
    QCOMPARE(result.error, LongshotError::None);
    QCOMPARE(result.parts.size(), 1);
    QCOMPARE(result.parts.front().size(), truth.size());
    std::vector<int> expectedBreaks;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            QCOMPARE(result.parts.front().pixel(x, y),
                     y < 16 && x >= 64 ? qRgb(255, 255, 255) : truth.pixel(x, y));
        }
        if (y < 16) expectedBreaks.push_back(y);
    }
    QCOMPARE(result.breakRows, expectedBreaks);
}

void tst_LongshotReliability::sparseScrollingColumnsAreNotSidebars_data()
{
    QTest::addColumn<int>("ruleRow");
    QTest::newRow("shared-sparse-rule") << 64;
    QTest::newRow("newly-revealed-rule") << 130;
}

void tst_LongshotReliability::sparseScrollingColumnsAreNotSidebars()
{
    QFETCH(int, ruleRow);
    QImage page = texture();
    for (int y = 0; y < page.height(); ++y) {
        for (int x = 0; x < 32; ++x) page.setPixel(x, y, qRgb(255, 255, 255));
    }
    for (int x = 20; x < 32; ++x) page.setPixel(x, ruleRow, qRgb(0, 0, 0));
    const QImage from = page.copy(0, 0, 128, 128);
    const QImage to = page.copy(0, 8, 128, 128);
    QCOMPARE(LongshotAnalyzer::movingColumnSpan(from, to, 8, {}), from.rect());
}

void tst_LongshotReliability::weakJoinRemainsMarkedAfterPause()
{
    const QImage page = texture();
    MemorySource source({page.copy(0, 0, 128, 128), page.copy(0, 64, 128, 128),
                         page.copy(0, 64, 128, 128)});
    AnalysisResult analysis;
    analysis.frameSize = QSize(128, 128);
    for (int i = 0; i < 3; ++i) {
        auto frame = LongshotAnalyzer::computeFeatures(
            page.copy(0, i == 0 ? 0 : 64, 128, 128), i * kFrameIntervalMs);
        frame.stationary = i == 2;
        analysis.frames.push_back(frame);
    }
    analysis.edges = {{0, 1, 64, 0.75}, {1, 2, 0, 1.0}};
    auto render = [&] {
        analysis.solve = PositionSolver::solve({0, 50, 100}, analysis.edges, 3, 128);
        return LongshotRenderer::renderSections(source, {}, 0, -1, {}, analysis, {}, {});
    };
    const auto paused = render();
    QCOMPARE(paused.error, LongshotError::None);
    QCOMPARE(paused.fullHeightPx, 192);
    QVERIFY(std::find(paused.lowConfidenceRows.begin(), paused.lowConfidenceRows.end(), 150)
            != paused.lowConfidenceRows.end());

    // An independent strong connection to the anchor really does resolve the uncertainty.
    analysis.edges.push_back({0, 2, 64, 0.95});
    const auto rejoined = render();
    QCOMPARE(rejoined.error, LongshotError::None);
    QVERIFY(rejoined.lowConfidenceRows.empty());

    // A strong but inconsistent closure rejected by the solver cannot clear it.
    // Include an invalid observation to check indices refer to the original input.
    analysis.edges = {{-1, 0, 0, 1.0}};
    for (int i = 0; i < 10; ++i) analysis.edges.push_back({0, 1, 64, 0.75});
    analysis.edges.push_back({1, 2, 0, 1.0});
    analysis.edges.push_back({0, 2, 300, 0.95});
    const auto rejected = render();
    QCOMPARE(rejected.error, LongshotError::None);
    QCOMPARE(analysis.solve.rejectedObservationIndices, std::vector<int>{12});
    QVERIFY(std::find(rejected.lowConfidenceRows.begin(), rejected.lowConfidenceRows.end(), 150)
            != rejected.lowConfidenceRows.end());
}

QTEST_GUILESS_MAIN(tst_LongshotReliability)
#include "tst_LongshotReliability.moc"
