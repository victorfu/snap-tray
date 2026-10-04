#include "longshot/PositionSolver.h"
#include "longshot/LongshotRenderer.h"
#include <QtTest>
using namespace SnapTray::Longshot;
class SectionSource final : public LongshotFrameSource {
public:
    bool open(const QString&, qint64, qint64, const QRect&) override { nextIndex = 0; return true; }
    std::optional<QImage> next(qint64* time) override {
        if (nextIndex == 5) return {};
        QImage image(96, 96, QImage::Format_RGB32);
        image.fill(nextIndex < 2 ? Qt::red : nextIndex < 4 ? Qt::blue : Qt::green);
        *time = nextIndex++ * 50; return image;
    }
    QSize frameSize() const override { return {96, 96}; }
    QSize videoSize() const override { return frameSize(); }
    double frameRate() const override { return 20; }
    int expectedFrameCount() const override { return 5; }
    QString lastError() const override { return {}; }
    int nextIndex = 0;
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
