#include "longshot/LongshotRenderer.h"
#include <QtTest>
using namespace SnapTray::Longshot;
namespace {
AnalysisResult fixture(int frameHeight = 100, int shift = 60, bool second = false) {
    AnalysisResult a; a.frameSize = QSize(64, frameHeight);
    const int count = second ? 4 : 2;
    for (int i = 0; i < count; ++i) {
        FrameFeatures f; f.tMs = i * 100; f.validContentRect = QRect(0,0,64,frameHeight);
        a.frames.push_back(f); a.solve.positions.push_back(std::nullopt);
        QImage thumb(16,25,QImage::Format_Grayscale8); thumb.fill(i * 30); a.thumbnails.push_back(thumb);
    }
    a.solve.sections.push_back({{0,1},{0,shift}});
    a.edges.push_back({0,1,shift,0.95});
    if (second) { a.solve.sections.push_back({{2,3},{0,shift+10}}); a.edges.push_back({2,3,shift+10,0.95}); }
    return a;
}
class Source : public LongshotFrameSource {
public:
    QSize size; int count = 2, index = 0, decoded = 0;
    explicit Source(QSize s) : size(s) {}
    bool open(const QString&, qint64, qint64, const QRect&) override { index=0; return true; }
    std::optional<QImage> next(qint64* time) override {
        if (index == count) return {};
        *time = index * 100; QImage image(size,QImage::Format_RGB32);
        image.fill(index++ ? Qt::blue : Qt::red); ++decoded; return image;
    }
    QSize frameSize() const override { return size; }
    QSize videoSize() const override { return size; }
    double frameRate() const override { return 10; }
    int expectedFrameCount() const override { return count; }
    QString lastError() const override { return {}; }
};
}
class TestLongshotCandidates : public QObject {
    Q_OBJECT
private slots:
    void rankingAndSelectedRendering() {
        auto a = fixture(100,60,true);
        auto candidates = LongshotRenderer::candidates(a,{});
        QCOMPARE(candidates.size(),size_t(2));
        QCOMPARE(candidates[0].startMs,qint64(200));
        QVERIFY(candidates[0].partial);
        a.edges[1].confidence = 0.75;
        candidates = LongshotRenderer::candidates(a,{});
        QCOMPARE(candidates[0].startMs,qint64(0));
        QVERIFY(!candidates[0].needsReview); QVERIFY(candidates[1].needsReview);
        Source source(a.frameSize); source.count=4;
        const auto result = LongshotRenderer::renderCandidate(source,{},0,-1,{},a,candidates[0],{});
        QCOMPARE(result.error,LongshotError::None);
        QCOMPARE(result.parts.size(),1); QCOMPARE(result.parts[0].size(),candidates[0].size);
        QCOMPARE(result.sectionCount,1); QVERIFY(result.breakRows.empty());
    }
    void splitAndOriginalPixels() {
        auto a=fixture(20000,15000);
        const auto candidates=LongshotRenderer::candidates(a,{});
        QCOMPARE(candidates.size(),size_t(1)); QCOMPARE(candidates[0].imageCount,2);
        QCOMPARE(candidates[0].size,QSize(64,35000));
        Source source(a.frameSize);
        const auto result=LongshotRenderer::renderCandidate(source,{},0,-1,{},a,candidates[0],{});
        QCOMPARE(result.error,LongshotError::None); QCOMPARE(result.parts.size(),2);
        QCOMPARE(result.parts[0].size(),QSize(64,30000)); QCOMPARE(result.parts[1].size(),QSize(64,5000));
        QCOMPARE(result.parts[0].pixelColor(0,0),QColor(Qt::red));
        QCOMPARE(result.parts[1].pixelColor(0,4999),QColor(Qt::blue));
        QCOMPARE(source.decoded,2);
    }
    void gapsBecomeSeparateCandidates() {
        auto a=fixture(100,60,true);
        a.solve.sections={{{0,1,2,3},{0,60,250,310}}};
        a.edges={{0,1,60,0.95},{1,2,190,0.95},{2,3,60,0.95}};
        const auto candidates=LongshotRenderer::candidates(a,{});
        QCOMPARE(candidates.size(),size_t(2));
        for(const auto& candidate:candidates) {
            QCOMPARE(candidate.size,QSize(64,160)); QVERIFY(candidate.partial);
            Source source(a.frameSize); source.count=4;
            const auto result=LongshotRenderer::renderCandidate(source,{},0,-1,{},a,candidate,{});
            QCOMPARE(result.error,LongshotError::None); QVERIFY(result.breakRows.empty());
        }
    }
    void stationaryAndCancelledAreNotCandidates() {
        QVERIFY(LongshotRenderer::candidates(fixture(100,0),{}).empty());
        QVERIFY(LongshotRenderer::candidates(fixture(),{},[](int){return false;}).empty());
        auto a=fixture(); a.solve.converged=false;
        QVERIFY(LongshotRenderer::candidates(a,{}).empty());
    }
    void longerDurationDoesNotChangeRanking() {
        auto a=fixture(100,60,true);
        a.frames[1].tMs=100000;
        const auto candidates=LongshotRenderer::candidates(a,{});
        QCOMPARE(candidates.front().size.height(),170);
    }
};
QTEST_GUILESS_MAIN(TestLongshotCandidates)
#include "tst_LongshotCandidates.moc"
