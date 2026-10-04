#include "longshot/PositionSolver.h"
#include "longshot/LongshotRenderer.h"
#include <QtTest>
using namespace SnapTray::Longshot;
class SectionSource final : public LongshotFrameSource {
public:
    bool open(const QString&, qint64, qint64, const QRect&) override { ++openCalls; nextIndex = 0; return !failOpen; }
    std::optional<QImage> next(qint64* time) override {
        ++nextCalls;
        if (nextIndex == int(groups.size()) || nextIndex == failAt) return {};
        ++decodedFrames;
        QImage image(96, 96, QImage::Format_RGB32);
        image.fill(groups[size_t(nextIndex)] == 0 ? Qt::red : groups[size_t(nextIndex)] == 1 ? Qt::blue : Qt::green);
        if (patterned) {
            for (int y=0;y<96;++y) for (int x=0;x<96;++x)
                image.setPixel(x,y,qRgb(groups[size_t(nextIndex)]*64+y/2,x,(nextIndex*17+y)%256));
        }
        *time = nextIndex++ * 50; return image;
    }
    QSize frameSize() const override { return {96, 96}; }
    QSize videoSize() const override { return frameSize(); }
    double frameRate() const override { return 20; }
    int expectedFrameCount() const override { return int(groups.size()); }
    QString lastError() const override { return nextIndex == failAt ? QStringLiteral("injected decode failure") : QString(); }
    std::vector<int> groups{0,0,1,1,2};
    int nextIndex = 0, openCalls = 0, nextCalls = 0, decodedFrames = 0;
    int failAt = -1;
    bool failOpen = false, patterned = false;
};
class tst_LongshotSections : public QObject {
    Q_OBJECT
private slots:
    void keepsConnectedSectionsButNotSingletons() {
        const auto solve = PositionSolver::solve({0,50,100,150,200}, {{0,1,0,1},{2,3,0,1}},3,96);
        QCOMPARE(solve.sections.size(), size_t(2));
        QCOMPARE(solve.sections[0].frameIndices, (std::vector<int>{0,1}));
        QCOMPARE(solve.sections[1].frameIndices, (std::vector<int>{2,3}));
        QVERIFY(!solve.positions[2]); // legacy projection is retained for the closure search
        QVERIFY(!solve.positions[4]);
    }
    void decodeWorkIsIndependentOfSectionCount_data() {
        QTest::addColumn<int>("sections");
        QTest::newRow("one") << 1;
        QTest::newRow("four") << 4;
        QTest::newRow("twelve") << 12;
    }
    void decodeWorkIsIndependentOfSectionCount() {
        QFETCH(int,sections);
        SectionSource source; source.groups.clear();
        AnalysisResult analysis; analysis.frameSize={96,96};
        std::vector<qint64> times;
        for(int i=0;i<sections*2;++i) {
            source.groups.push_back(i/2); times.push_back(i*50);
            FrameFeatures f; f.tMs=i*50; f.validContentRect={0,0,96,96}; analysis.frames.push_back(f);
            if(i%2) analysis.edges.push_back({i-1,i,0,1});
        }
        analysis.solve=PositionSolver::solve(times,analysis.edges,3,96);
        int previous=-1;
        auto result=LongshotRenderer::renderSections(source,{},0,-1,{},analysis,{},[&](int p) {
            const bool monotonic=p>=previous; previous=p; return monotonic;
        });
        QCOMPARE(result.error,LongshotError::None); QCOMPARE(result.sectionCount,sections);
        QCOMPARE(source.openCalls,1); QCOMPARE(source.decodedFrames,sections*2);
        QCOMPARE(source.nextCalls,sections*2+1); QCOMPARE(previous,100);
    }
    void interleavedSectionsMatchSeparateRenders() {
        SectionSource source; source.groups={0,1,0,1,2}; source.patterned=true;
        AnalysisResult analysis; analysis.frameSize={96,96};
        analysis.edges={{0,2,32,0.8},{1,3,-16,0.8}};
        analysis.solve=PositionSolver::solve({0,50,100,150,200},analysis.edges,3,96);
        for(int i=0;i<5;++i) {
            FrameFeatures f; f.tMs=i*50; f.validContentRect={8,4,80,88}; f.excludedBands={4,4,8,8};
            analysis.frames.push_back(f);
        }
        LongshotOptions options; options.includeStickyHeader=true; options.splitOversize=true; options.maxHeightPx=64;
        auto result=LongshotRenderer::renderSections(source,{},0,-1,{},analysis,options,{});
        QCOMPARE(result.error,LongshotError::None); QCOMPARE(source.decodedFrames,5); QCOMPARE(source.openCalls,1);
        QCOMPARE(result.parts[0].pixelColor(0,0),QColor(0,8,0));
        QCOMPARE(result.parts[2].pixelColor(0,0),QColor(64,8,17));
        QCOMPARE(result.parts[1].pixelColor(0,59),QColor(45,8,125));
        QCOMPARE(result.parts[3].pixelColor(0,43),QColor(109,8,108));
        int firstPart=0, offset=0;
        std::vector<int> expectedLowRows, expectedBreakRows;
        std::vector<SourceSpan> expectedSpans;
        for(int section=0;section<2;++section) {
            auto single=analysis; single.solve.positions.assign(5,std::nullopt);
            const auto& group=analysis.solve.sections[size_t(section)];
            for(size_t j=0;j<group.frameIndices.size();++j) single.solve.positions[size_t(group.frameIndices[j])]=group.positions[j];
            SectionSource separate; separate.groups=source.groups; separate.patterned=true;
            const auto expected=LongshotRenderer::render(separate,{},0,-1,{},single,options,{});
            QCOMPARE(expected.error,LongshotError::None);
            for(const auto& part:expected.parts) { QCOMPARE(result.parts[firstPart++],part); }
            for(int row:expected.lowConfidenceRows) expectedLowRows.push_back(row+offset);
            for(int row:expected.breakRows) expectedBreakRows.push_back(row+offset);
            for(auto span:expected.sourceSpans) { span.firstRow+=offset; span.endRow+=offset; expectedSpans.push_back(span); }
            offset+=expected.fullHeightPx;
        }
        QCOMPARE(firstPart,result.parts.size()); QCOMPARE(result.fullHeightPx,offset);
        QCOMPARE(result.lowConfidenceRows,expectedLowRows); QCOMPARE(result.breakRows,expectedBreakRows);
        QCOMPARE(result.sourceSpans.size(),expectedSpans.size());
        for(size_t i=0;i<expectedSpans.size();++i) {
            QCOMPARE(result.sourceSpans[i].firstRow,expectedSpans[i].firstRow);
            QCOMPARE(result.sourceSpans[i].endRow,expectedSpans[i].endRow);
            QCOMPARE(result.sourceSpans[i].timeMs,expectedSpans[i].timeMs);
        }
    }
    void failedDecodeDoesNotPublishParts_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("open") << -2;
        QTest::newRow("middle") << 2;
        QTest::newRow("unused-tail") << 4;
        QTest::newRow("before-open-cancellation") << -3;
    }
    void failedDecodeDoesNotPublishParts() {
        QFETCH(int,failure);
        AnalysisResult a; a.frameSize={96,96}; a.edges={{0,1,0,1},{2,3,0,1}};
        a.solve=PositionSolver::solve({0,50,100,150,200},a.edges,3,96);
        for(int i=0;i<5;++i) { FrameFeatures f; f.validContentRect={0,0,96,96}; a.frames.push_back(f); }
        SectionSource source; source.failOpen=failure==-2; source.failAt=failure;
        const auto result=LongshotRenderer::renderSections(source,{},0,-1,{},a,{},[&](int) { return failure!=-3; });
        QCOMPARE(result.error,failure==-3?LongshotError::Cancelled:LongshotError::SourceUnavailable);
        QVERIFY(result.parts.isEmpty()); QVERIFY(result.partInfo.isEmpty());
        QCOMPARE(source.openCalls,failure==-3?0:1);
    }
    void rendersSeparateSectionsAndSplitsWithinEach() {
        AnalysisResult analysis;
        analysis.frameSize = {96,96}; analysis.frameRate = 20;
        analysis.edges = {{0,1,0,1},{2,3,0,1}};
        analysis.solve = PositionSolver::solve({0,50,100,150,200}, analysis.edges,3,96);
        for (int i = 0; i < 5; ++i) {
            FrameFeatures f; f.tMs = i*50; f.validContentRect = QRect(0,0,96,96); f.stationary = true;
            analysis.frames.push_back(f);
        }
        SectionSource source;
        LongshotOptions options; options.maxHeightPx = 64; options.splitOversize = true;
        auto result = LongshotRenderer::renderSections(source, {}, 0,-1,{},analysis,options,{});
        QCOMPARE(result.error, LongshotError::None);
        QCOMPARE(source.openCalls,1);
        QCOMPARE(source.decodedFrames,5);
        QCOMPARE(source.nextCalls,6); // five frames plus the terminal read
        QCOMPARE(result.sectionCount,2); QCOMPARE(result.parts.size(),4);
        QCOMPARE(result.fullHeightPx,192);
        QCOMPARE(result.parts[0].size(),QSize(96,64));
        QCOMPARE(result.parts[1].size(),QSize(96,32));
        QCOMPARE(result.parts[0].pixelColor(0,0),QColor(Qt::red));
        QCOMPARE(result.parts[2].pixelColor(0,0),QColor(Qt::blue));
        QCOMPARE(result.partInfo[2].section,1);
        QCOMPARE(result.partInfo[2].indexInSection,0);
        QCOMPARE(result.partInfo[2].partsInSection,2);
        QCOMPARE(result.partInfo[2].startMs,100);
        QVERIFY(std::any_of(result.sourceSpans.begin(),result.sourceSpans.end(),[](const auto& span) {
            return span.firstRow >= 96 && span.timeMs >= 100;
        }));
        int previous = -1;
        result = LongshotRenderer::renderSections(source,{},0,-1,{},analysis,options,[&](int p) {
            if (p < previous) return false; previous = p; return p < 50;
        });
        QCOMPARE(result.error,LongshotError::Cancelled);
        QVERIFY(result.parts.isEmpty()); // cancellation never presents a partial run as success
    }
};
QTEST_GUILESS_MAIN(tst_LongshotSections)
#include "tst_LongshotSections.moc"
