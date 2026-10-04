#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotRenderer.h"
#include "longshot/LongshotSession.h"

#include <QtTest>
#include <QRandomGenerator>
#include <QTemporaryFile>

using namespace SnapTray::Longshot;

namespace {
class MemorySource final : public LongshotFrameSource
{
public:
    explicit MemorySource(std::vector<QImage> frames) : m_frames(std::move(frames)) {}
    bool open(const QString&, qint64, qint64, const QRect&) override { m_next = 0; return true; }
    std::optional<QImage> next(qint64* tMs) override
    {
        if (m_next == m_frames.size()) return std::nullopt;
        if (tMs) *tMs = qint64(m_next) * 50;
        return m_frames[m_next++];
    }
    QSize frameSize() const override { return m_frames.front().size(); }
    QSize videoSize() const override { return frameSize(); }
    double frameRate() const override { return 20.0; }
    int expectedFrameCount() const override { return int(m_frames.size()); }
    QString lastError() const override { return {}; }
private:
    std::vector<QImage> m_frames;
    size_t m_next = 0;
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
    void unmatchedFramesAreRejected_data();
    void unmatchedFramesAreRejected();
    void texturedStationaryFramesRemainValid();
    void failedSessionDoesNotCacheSuccess();
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

QTEST_GUILESS_MAIN(tst_LongshotReliability)
#include "tst_LongshotReliability.moc"
