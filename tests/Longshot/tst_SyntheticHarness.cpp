#include "SyntheticScroll.h"
#include "longshot/FrameReaderLongshotSource.h"

#include <QPainter>
#include <QtTest>
#include <QTemporaryDir>

using namespace SyntheticScroll;

namespace {

// Mean absolute luma difference between two same-sized images, 0..255.
double meanAbsDiff(const QImage& a, const QImage& b)
{
    Q_ASSERT(a.size() == b.size());
    double sum = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) sum += qAbs(qGray(a.pixel(x, y)) - qGray(b.pixel(x, y)));
    }
    return sum / (double(a.width()) * a.height());
}

// Loose bound for H.264 at intermediate quality on text-like content.
constexpr double kMaxCodecError = 6.0;

} // namespace

class tst_SyntheticHarness : public QObject
{
    Q_OBJECT
private slots:
    void pageIsDeterministicAndBusy();
    void trajectoriesClamp();
    void frameIsPageWindow();
    void disturbancesPaintWhereExpected();
    void compareIdenticalIsClean();
    void compareDetectsDuplicatesMissingAndShift();
    void encodedFramesDecodeCloseToSource();
};

void tst_SyntheticHarness::pageIsDeterministicAndBusy()
{
    PageSpec spec;
    const QImage a = renderPage(spec);
    const QImage b = renderPage(spec);
    QCOMPARE(a, b);
    QCOMPARE(a.size(), QSize(spec.width, spec.height));
    // Every 64-row band must contain ink, so no viewport-sized blank exists by default.
    for (int y = 0; y + 64 <= a.height(); y += 64) {
        int dark = 0;
        for (int yy = y; yy < y + 64; ++yy) {
            for (int x = 0; x < a.width(); x += 8) dark += qGray(a.pixel(x, yy)) < 128;
        }
        QVERIFY2(dark > 0, qPrintable(QStringLiteral("blank band at %1").arg(y)));
    }
    spec.seed = 2;
    QVERIFY(renderPage(spec) != a);
}

void tst_SyntheticHarness::trajectoriesClamp()
{
    const Trajectory t = clampTrajectory(constantSpeed(50, 3900, 40), 4000, kViewport.height());
    QCOMPARE(t.offsets.size(), size_t(50));
    for (int o : t.offsets) QVERIFY(o >= 0 && o <= 4000 - kViewport.height());
    const Trajectory f = fling(30, 0, 120);
    QVERIFY(f.offsets[1] - f.offsets[0] >= f.offsets[29] - f.offsets[28]);
    QCOMPARE(f.offsets[29], f.offsets[28]); // settles
    const Trajectory b = backAndForth(40, 500, 300, 30);
    QVERIFY(*std::max_element(b.offsets.begin(), b.offsets.end()) <= 800);
    QVERIFY(*std::min_element(b.offsets.begin(), b.offsets.end()) >= 200);
    const Trajectory p = withPauses(constantSpeed(10, 0, 10), 3, 2);
    QCOMPARE(p.offsets.size(), size_t(10 + 3 * 2)); // a pause after frames 3, 6, 9
    QCOMPARE(p.offsets[3], p.offsets[4]);
}

void tst_SyntheticHarness::frameIsPageWindow()
{
    const QImage page = renderPage(PageSpec{});
    const Trajectory t = constantSpeed(3, 100, 50);
    const QImage frame = renderFrame(page, kViewport, t, 2, Disturbances{});
    QCOMPARE(frame.size(), kViewport);
    QCOMPARE(frame.format(), QImage::Format_RGB32);
    QCOMPARE(meanAbsDiff(frame, page.copy(0, 200, kViewport.width(), kViewport.height()).convertToFormat(QImage::Format_RGB32)), 0.0);
}

void tst_SyntheticHarness::disturbancesPaintWhereExpected()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.stickyHeaderHeight = 40;
    d.sidebarWidth = 100;
    const Trajectory t = constantSpeed(3, 100, 50);
    const QImage frame = renderFrame(page, kViewport, t, 1, d);
    // Header and sidebar are opaque and identical regardless of offset.
    const QImage other = renderFrame(page, kViewport, t, 2, d);
    QCOMPARE(frame.copy(0, 0, kViewport.width(), 40), other.copy(0, 0, kViewport.width(), 40));
    QCOMPARE(frame.copy(kViewport.width() - 100, 0, 100, kViewport.height()),
             other.copy(kViewport.width() - 100, 0, 100, kViewport.height()));
    // Scroll-up header only on frames whose offset decreased.
    Disturbances up;
    up.scrollUpHeaderHeight = 48;
    const Trajectory bf = backAndForth(6, 400, 200, 100); // goes down then up
    int firstUpFrame = -1;
    for (size_t i = 1; i < bf.offsets.size(); ++i) {
        if (bf.offsets[i] < bf.offsets[i - 1]) { firstUpFrame = int(i); break; }
    }
    QVERIFY(firstUpFrame > 0);
    const QImage down = renderFrame(page, kViewport, bf, 1, up);
    const QImage upFrame = renderFrame(page, kViewport, bf, firstUpFrame, up);
    QVERIFY(down.copy(0, 0, kViewport.width(), 48) != upFrame.copy(0, 0, kViewport.width(), 48));
    QCOMPARE(upFrame.copy(0, 0, kViewport.width(), 48),
             renderFrame(page, kViewport, bf, firstUpFrame, up).copy(0, 0, kViewport.width(), 48));
}

void tst_SyntheticHarness::compareIdenticalIsClean()
{
    const QImage page = renderPage(PageSpec{});
    const QImage slice = page.copy(0, 500, page.width(), 1000);
    const RowMatchReport r = compareWithGroundTruth(slice, page, 500, 1499);
    QCOMPARE(r.outputRows, 1000);
    QCOMPARE(r.matchedRows, 1000);
    QCOMPARE(r.duplicatedRows, 0);
    QCOMPARE(r.missingRows, 0);
    QCOMPARE(r.misalignedRows, 0);
    QCOMPARE(r.unmatchedRows, 0);
}

void tst_SyntheticHarness::compareDetectsDuplicatesMissingAndShift()
{
    const QImage page = renderPage(PageSpec{});
    // Duplicate 10 rows: copy rows 500-999 then 990-1499.
    QImage dup(page.width(), 1010, QImage::Format_RGB32);
    QPainter p(&dup);
    p.drawImage(0, 0, page, 0, 500, page.width(), 500);
    p.drawImage(0, 500, page, 0, 990, page.width(), 510);
    p.end();
    RowMatchReport r = compareWithGroundTruth(dup, page, 500, 1499);
    QVERIFY2(r.duplicatedRows >= 9 && r.duplicatedRows <= 11, qPrintable(QString::number(r.duplicatedRows)));
    QCOMPARE(r.missingRows, 0);
    // Skip 10 rows: rows 500-999 then 1010-1499.
    QImage gap(page.width(), 990, QImage::Format_RGB32);
    QPainter g(&gap);
    g.drawImage(0, 0, page, 0, 500, page.width(), 500);
    g.drawImage(0, 500, page, 0, 1010, page.width(), 490);
    g.end();
    r = compareWithGroundTruth(gap, page, 500, 1499);
    QVERIFY2(r.missingRows >= 9 && r.missingRows <= 11, qPrintable(QString::number(r.missingRows)));
    QCOMPARE(r.duplicatedRows, 0);
    // A corrupted row (inverted) is not a page row: it must count as unmatched, not be forced onto a neighbour.
    QImage corrupted = page.copy(0, 500, page.width(), 200);
    QImage row = corrupted.copy(0, 100, page.width(), 1);
    row.invertPixels();
    QPainter s(&corrupted);
    s.drawImage(0, 100, row);
    s.end();
    r = compareWithGroundTruth(corrupted, page, 500, 699);
    QCOMPARE(r.unmatchedRows, 1);
    QVERIFY(r.matchedRows >= 199);
    QCOMPARE(r.duplicatedRows, 0);
}

void tst_SyntheticHarness::encodedFramesDecodeCloseToSource()
{
    auto source = SnapTray::Longshot::FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QImage page = renderPage(PageSpec{});
    const Trajectory t = clampTrajectory(constantSpeed(40, 0, 30), page.height(), kViewport.height());
    std::vector<QImage> frames;
    for (int i = 0; i < 40; ++i) frames.push_back(renderFrame(page, kViewport, t, i, Disturbances{}));
    const QString path = dir.filePath(QStringLiteral("constant.mp4"));
    const QString error = encodeFrames(path, frames, kFrameRate);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(source->open(path, 0, -1, QRect()), qPrintable(source->lastError()));
    int index = 0;
    qint64 tMs = 0;
    while (auto decoded = source->next(&tMs)) {
        QVERIFY(index < 40);
        const double err = meanAbsDiff(*decoded, frames[index]);
        QVERIFY2(err < kMaxCodecError, qPrintable(QStringLiteral("frame %1 error %2").arg(index).arg(err)));
        ++index;
    }
    QCOMPARE(index, 40);
}

QTEST_MAIN(tst_SyntheticHarness)
#include "tst_SyntheticHarness.moc"
