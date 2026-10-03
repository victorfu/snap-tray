#include "SyntheticScroll.h"
#include "longshot/LongshotSession.h"

#include <QtTest>
#include <QTemporaryDir>

using namespace SnapTray::Longshot;
using namespace SyntheticScroll;

namespace {

// Serves pre-rendered frames at 50 ms intervals and counts decodes.
class FakeSource final : public LongshotFrameSource
{
public:
    FakeSource(std::shared_ptr<std::vector<QImage>> frames, std::shared_ptr<int> decodes) : m_frames(frames), m_decodes(decodes) {}
    bool open(const QString&, qint64 startMs, qint64 endMs, const QRect& crop) override
    {
        m_crop = crop.isEmpty() ? m_frames->front().rect() : crop;
        m_start = int(startMs / 50);
        m_end = endMs < 0 ? int(m_frames->size()) : int(std::min<qint64>(m_frames->size(), (endMs + 49) / 50));
        m_next = m_start;
        return true;
    }
    std::optional<QImage> next(qint64* tMs) override
    {
        if (m_next >= m_end) return std::nullopt;
        ++*m_decodes;
        if (tMs) *tMs = qint64(m_next) * 50;
        return (*m_frames)[size_t(m_next++)].copy(m_crop);
    }
    QSize frameSize() const override { return m_crop.size(); }
    QSize videoSize() const override { return m_frames->front().size(); }
    double frameRate() const override { return 20.0; }
    int expectedFrameCount() const override { return m_end - m_start; }
    QString lastError() const override { return {}; }
private:
    std::shared_ptr<std::vector<QImage>> m_frames;
    std::shared_ptr<int> m_decodes;
    QRect m_crop;
    int m_start = 0, m_end = 0, m_next = 0;
};

} // namespace

class tst_LongshotSession : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void decodeCacheIsBoundedLru();
    void decodeCacheRequiresSameIdentity();
    void identicalRunReusesEverything();
    void optionsChangeReusesAnalysis();
    void trimExtensionReusesOverlapFeatures();
    void cropChangeInvalidatesAll();
    void sourceChangeInvalidatesAll();

private:
    std::shared_ptr<std::vector<QImage>> m_frames = std::make_shared<std::vector<QImage>>();
    std::shared_ptr<int> m_decodes = std::make_shared<int>(0);
    QTemporaryDir m_dir;
    QString m_fileA;
    QString m_fileB;

    LongshotSession makeSession()
    {
        return LongshotSession([this]() { return std::make_unique<FakeSource>(m_frames, m_decodes); });
    }
};

void tst_LongshotSession::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QImage page = renderPage(PageSpec{});
    const Trajectory t = clampTrajectory(constantSpeed(40, 0, 45), page.height(), kViewport.height());
    for (int i = 0; i < 40; ++i) m_frames->push_back(renderFrame(page, kViewport, t, i, Disturbances{}));
    // Source identity comes from the file, so two distinct files stand in for two recordings.
    m_fileA = m_dir.filePath(QStringLiteral("a.mp4"));
    m_fileB = m_dir.filePath(QStringLiteral("b.mp4"));
    QFile a(m_fileA); QVERIFY(a.open(QIODevice::WriteOnly)); a.write("aaaa"); a.close();
    QFile b(m_fileB); QVERIFY(b.open(QIODevice::WriteOnly)); b.write("bbbbbbbb"); b.close();
}

void tst_LongshotSession::decodeCacheIsBoundedLru()
{
    const SourceIdentity id = SourceIdentity::fromFile(m_fileA);
    QImage frame(100, 100, QImage::Format_RGB32); // 40,000 bytes
    DecodedFrameCache cache(100000);                 // room for two
    cache.store(id, 0, frame);
    cache.store(id, 50, frame);
    QCOMPARE(cache.count(), 2);
    QVERIFY(cache.find(id, 0).has_value());        // touch 0 -> 50 is now least recent
    cache.store(id, 100, frame);
    QCOMPARE(cache.count(), 2);
    QVERIFY(cache.find(id, 0).has_value());
    QVERIFY(!cache.find(id, 50).has_value());
    QVERIFY(cache.find(id, 100).has_value());
    QVERIFY(cache.bytes() <= 100000);
    QImage huge(300, 300, QImage::Format_RGB32);     // 360,000 bytes > budget
    cache.store(id, 150, huge);
    QVERIFY(!cache.find(id, 150).has_value());
    cache.clear();
    QCOMPARE(cache.count(), 0);
    QCOMPARE(cache.bytes(), qint64(0));
}

void tst_LongshotSession::decodeCacheRequiresSameIdentity()
{
    DecodedFrameCache cache(1000000);
    QImage frame(10, 10, QImage::Format_RGB32);
    cache.store(SourceIdentity::fromFile(m_fileA), 0, frame);
    QVERIFY(cache.find(SourceIdentity::fromFile(m_fileA), 0).has_value());
    QVERIFY(!cache.find(SourceIdentity::fromFile(m_fileB), 0).has_value());
    QVERIFY(!cache.find(SourceIdentity::fromFile(m_fileA), 50).has_value());
}

void tst_LongshotSession::identicalRunReusesEverything()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    *m_decodes = 0;
    const RunReport first = session.run({});
    QCOMPARE(int(first.error), int(LongshotError::None));
    QVERIFY(first.framesAnalyzed == 40);
    QVERIFY(*m_decodes > 0);
    const int decodesAfterFirst = *m_decodes;
    const RunReport second = session.run({});
    QVERIFY(second.reusedFeatures);
    QVERIFY(second.reusedSolve);
    QVERIFY(second.reusedRender);
    QCOMPARE(*m_decodes, decodesAfterFirst);
    QCOMPARE(second.render.parts.first(), first.render.parts.first());
}

void tst_LongshotSession::optionsChangeReusesAnalysis()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    session.run({});
    *m_decodes = 0;
    LongshotOptions options;
    options.maxHeightPx = 500;
    session.setOptions(options);
    const RunReport report = session.run({});
    QVERIFY(report.reusedFeatures);
    QVERIFY(report.reusedSolve);
    QVERIFY(!report.reusedRender);
    QCOMPARE(report.framesAnalyzed, 0);
    QVERIFY(report.render.heightCapped);
    QCOMPARE(*m_decodes, 40); // one render pass, no analysis pass
}

void tst_LongshotSession::trimExtensionReusesOverlapFeatures()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    session.setTrim(0, 1000); // frames 0..19
    const RunReport first = session.run({});
    QCOMPARE(first.framesAnalyzed, 20);
    session.setTrim(0, 2000); // frames 0..39
    *m_decodes = 0;
    const RunReport extended = session.run({});
    QVERIFY(extended.reusedFeatures);
    QVERIFY(!extended.reusedSolve);
    QVERIFY(!extended.reusedRender);
    QCOMPARE(extended.framesAnalyzed, 20); // only the newly included frames
    QCOMPARE(extended.analysis.frames.size(), size_t(40));
    for (size_t i = 0; i < 40; ++i) QCOMPARE(extended.analysis.frames[i].tMs, qint64(i) * 50);
    QVERIFY(extended.render.parts.first().height() > first.render.parts.first().height());
    // Shrinking the trim drops out-of-range frames without re-analysing anything.
    session.setTrim(500, 1500);
    *m_decodes = 0;
    const RunReport shrunk = session.run({});
    QVERIFY(shrunk.reusedFeatures);
    QCOMPARE(shrunk.framesAnalyzed, 0);
    QCOMPARE(shrunk.analysis.frames.size(), size_t(20));
    QCOMPARE(shrunk.analysis.frames.front().tMs, qint64(500));
}

void tst_LongshotSession::cropChangeInvalidatesAll()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    session.run({});
    session.setCrop(QRect(0, 0, 400, 480));
    *m_decodes = 0;
    const RunReport report = session.run({});
    QVERIFY(!report.reusedFeatures);
    QVERIFY(!report.reusedSolve);
    QVERIFY(!report.reusedRender);
    QCOMPARE(report.framesAnalyzed, 40);
    QCOMPARE(report.analysis.frameSize, QSize(400, 480));
    QCOMPARE(report.render.parts.first().width(), 400 - report.render.autoCroppedLeft - report.render.autoCroppedRight);
}

void tst_LongshotSession::sourceChangeInvalidatesAll()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    session.run({});
    session.setRecording(m_fileB);
    const RunReport report = session.run({});
    QVERIFY(!report.reusedFeatures);
    QCOMPARE(report.framesAnalyzed, 40);
}

QTEST_MAIN(tst_LongshotSession)
#include "tst_LongshotSession.moc"
