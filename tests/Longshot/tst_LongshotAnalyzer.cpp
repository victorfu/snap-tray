#include "SyntheticScroll.h"
#include "longshot/LongshotAnalyzer.h"

#include <QPainter>
#include <QtTest>

using namespace SnapTray::Longshot;
using namespace SyntheticScroll;

namespace {

struct Pair {
    QImage from;
    QImage to;
    FrameFeatures fromFeatures;
    FrameFeatures toFeatures;
};

Pair makePair(const QImage& page, int offsetFrom, int offsetTo, const Disturbances& d = {})
{
    Trajectory t;
    t.offsets = {offsetFrom, offsetTo};
    Pair p;
    p.from = renderFrame(page, kViewport, t, 0, d);
    p.to = renderFrame(page, kViewport, t, 1, d);
    p.fromFeatures = LongshotAnalyzer::computeFeatures(p.from, 0);
    p.toFeatures = LongshotAnalyzer::computeFeatures(p.to, 50);
    return p;
}

constexpr int kShiftTolerance = 0;

} // namespace

class tst_LongshotAnalyzer : public QObject
{
    Q_OBJECT
private slots:
    void featuresHaveOneEntryPerRow();
    void recoversKnownShift_data();
    void recoversKnownShift();
    void stationaryIsDetected();
    void uniformRegionIsAmbiguous();
    void periodicContentIsAmbiguous();
    void collapsedCandidatesStillSeeTheRunnerUp();
    void stickyHeaderBecomesTopBand();
    void tallHeaderStillMatches();
    void sidebarExcludedFromMovingSpan();
    void scrollUpHeaderIsPerFrame();
    void hoverChangeDoesNotBreakShift();
    void blankBandInTemplateIsNotAMatch();
    void horizontalShiftIsRefused();
    void zoomIsRefused();
};

void tst_LongshotAnalyzer::featuresHaveOneEntryPerRow()
{
    const QImage page = renderPage(PageSpec{});
    const Pair p = makePair(page, 100, 100);
    QCOMPARE(p.fromFeatures.rowMean.size(), size_t(kViewport.height()));
    QCOMPARE(p.fromFeatures.rowGradient.size(), size_t(kViewport.height()));
    QCOMPARE(p.fromFeatures.validContentRect, QRect(QPoint(0, 0), kViewport));
    // A text row is darker than the white gap above it.
    QVERIFY(*std::min_element(p.fromFeatures.rowMean.begin(), p.fromFeatures.rowMean.end()) < 250.0f);
    QVERIFY(*std::max_element(p.fromFeatures.rowGradient.begin(), p.fromFeatures.rowGradient.end()) > 2.0f);
}

void tst_LongshotAnalyzer::recoversKnownShift_data()
{
    QTest::addColumn<int>("dy");
    QTest::newRow("down 7") << 7;
    QTest::newRow("down 40") << 40;
    QTest::newRow("up 33") << -33;
    QTest::newRow("down 150") << 150;
    QTest::newRow("down 230 (near half viewport)") << 230;
}

void tst_LongshotAnalyzer::recoversKnownShift()
{
    QFETCH(int, dy);
    const QImage page = renderPage(PageSpec{});
    const Pair p = makePair(page, 1000, 1000 + dy);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QCOMPARE(obs->shift.from, 0);
    QCOMPARE(obs->shift.to, 1);
    QVERIFY2(qAbs(obs->shift.dy - dy) <= kShiftTolerance,
             qPrintable(QStringLiteral("dy %1 expected %2").arg(obs->shift.dy).arg(dy)));
    QVERIFY2(obs->shift.confidence >= 0.5, qPrintable(QString::number(obs->shift.confidence)));
    QVERIFY(!obs->stationary);
    QCOMPARE(obs->bandsFrom, StaticBands{});
    QCOMPARE(obs->bandsTo, StaticBands{});
}

void tst_LongshotAnalyzer::stationaryIsDetected()
{
    const QImage page = renderPage(PageSpec{});
    const Pair p = makePair(page, 700, 700);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 3, 4, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QVERIFY(obs->stationary);
    QCOMPARE(obs->shift.dy, 0);
    QCOMPARE(obs->shift.confidence, 1.0);
}

void tst_LongshotAnalyzer::uniformRegionIsAmbiguous()
{
    PageSpec spec;
    spec.blankTop = 1500;
    spec.blankHeight = 1400; // taller than two viewports
    const QImage page = renderPage(spec);
    // Both frames entirely inside the blank band: nothing to match on.
    const Pair p = makePair(page, 1600, 1650);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    // Two blank frames are indistinguishable from a stationary pair; without
    // ink the analyzer must refuse to call them either.
    QVERIFY(!obs.has_value());
    const Pair same = makePair(page, 1600, 1600);
    QVERIFY(!LongshotAnalyzer::estimateShift(same.from, same.fromFeatures, same.to, same.toFeatures, 0, 1, AnalyzerParams{}).has_value());
}

namespace {
constexpr int kPeriodPitch = 44;

// A 600-row page whose middle repeats exactly every 44 rows (12-row bars),
// with one unique block at each end so the two frames are not identical.
// `ramp` adds a faint background gradient (1 luma per 8 rows) so unshifted
// rows differ and the static-band pre-pass cannot call the bars an overlay;
// the bars themselves stay periodic.
QImage periodicPage(bool ramp)
{
    QImage page(kViewport.width(), 600, QImage::Format_RGB32);
    page.fill(Qt::white);
    {
        QPainter painter(&page);
        for (int y = 0; ramp && y < page.height(); ++y) {
            const int luma = 255 - y / 8;
            painter.fillRect(QRect(0, y, page.width(), 1), QColor(luma, luma, luma));
        }
        for (int y = 0; y + 12 <= page.height(); y += kPeriodPitch) {
            if (y + 12 > 20 && y < 40) continue;   // keep the unique top block clean
            if (y + 12 > 500 && y < 520) continue; // and the unique bottom block
            painter.fillRect(QRect(24, y, page.width() - 48, 12), QColor(30, 30, 30));
        }
        painter.fillRect(QRect(24, 20, page.width() - 48, 20), QColor(200, 60, 60));
        painter.fillRect(QRect(24, 500, page.width() - 48, 20), QColor(60, 60, 200));
    }
    return page;
}
} // namespace

void tst_LongshotAnalyzer::periodicContentIsAmbiguous()
{
    const Pair p = makePair(periodicPage(true), 0, kPeriodPitch);
    // Many shifts fit equally well, so the pair must be refused, not guessed.
    QVERIFY(!LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{}).has_value());
}

void tst_LongshotAnalyzer::collapsedCandidatesStillSeeTheRunnerUp()
{
    // Pure periodic bars on white: coarse peaks near the true shift used to
    // collapse onto one dy after refinement, hiding the real alternatives.
    const Pair p = makePair(periodicPage(false), 0, kPeriodPitch);
    QVERIFY(!LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{}).has_value());
}

void tst_LongshotAnalyzer::stickyHeaderBecomesTopBand()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.stickyHeaderHeight = 40;
    const Pair p = makePair(page, 1000, 1060, d);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QVERIFY2(qAbs(obs->shift.dy - 60) <= kShiftTolerance, qPrintable(QString::number(obs->shift.dy)));
    QVERIFY2(qAbs(obs->bandsFrom.top - 40) <= 2, qPrintable(QString::number(obs->bandsFrom.top)));
    QVERIFY2(qAbs(obs->bandsTo.top - 40) <= 2, qPrintable(QString::number(obs->bandsTo.top)));
    QCOMPARE(obs->bandsTo.bottom, 0);
}

void tst_LongshotAnalyzer::tallHeaderStillMatches()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.stickyHeaderHeight = 300; // more than half the 480 px viewport
    const Pair p = makePair(page, 1000, 1040, d);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    // The 180 content rows below the header are enough to match on.
    QVERIFY(obs.has_value());
    QVERIFY2(qAbs(obs->shift.dy - 40) <= kShiftTolerance, qPrintable(QString::number(obs->shift.dy)));
    QVERIFY2(obs->bandsTo.top >= 290, qPrintable(QString::number(obs->bandsTo.top)));
}

void tst_LongshotAnalyzer::sidebarExcludedFromMovingSpan()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.sidebarWidth = 100;
    const Pair p = makePair(page, 1000, 1050, d);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QVERIFY2(qAbs(obs->shift.dy - 50) <= kShiftTolerance, qPrintable(QString::number(obs->shift.dy)));
    QVERIFY2(obs->movingSpanTo.right() <= kViewport.width() - 100 + 2, qPrintable(QString::number(obs->movingSpanTo.right())));
    QVERIFY(obs->movingSpanTo.left() <= 30);
    QCOMPARE(obs->movingSpanTo.height(), kViewport.height());
}

void tst_LongshotAnalyzer::scrollUpHeaderIsPerFrame()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.scrollUpHeaderHeight = 48;
    // Three frames: 1200 (scrolling down, no header), 1140 and 1080 (both
    // scrolling up, both show the header). A header is only detectable where
    // both frames of a pair show it, so the down->up pair reports no band
    // while the up->up pair reports it for both of its frames. Task 5
    // resolves per frame, so frame 1140 ends up with the band and frame 1200
    // without one — the header never becomes a global crop.
    Trajectory t;
    t.offsets = {1200, 1140, 1080};
    const QImage f0 = renderFrame(page, kViewport, t, 0, d);
    const QImage f1 = renderFrame(page, kViewport, t, 1, d);
    const QImage f2 = renderFrame(page, kViewport, t, 2, d);
    const auto downUp = LongshotAnalyzer::estimateShift(f0, LongshotAnalyzer::computeFeatures(f0, 0), f1,
                                                        LongshotAnalyzer::computeFeatures(f1, 50), 0, 1, AnalyzerParams{});
    QVERIFY(downUp.has_value());
    QVERIFY2(qAbs(downUp->shift.dy + 60) <= kShiftTolerance, qPrintable(QString::number(downUp->shift.dy)));
    QCOMPARE(downUp->bandsFrom.top, 0);
    const auto upUp = LongshotAnalyzer::estimateShift(f1, LongshotAnalyzer::computeFeatures(f1, 50), f2,
                                                      LongshotAnalyzer::computeFeatures(f2, 100), 1, 2, AnalyzerParams{});
    QVERIFY(upUp.has_value());
    QVERIFY2(qAbs(upUp->shift.dy + 60) <= kShiftTolerance, qPrintable(QString::number(upUp->shift.dy)));
    QVERIFY2(upUp->bandsTo.top >= 44 && upUp->bandsTo.top <= 52, qPrintable(QString::number(upUp->bandsTo.top)));
    QCOMPARE(upUp->bandsFrom.top, upUp->bandsTo.top);
}

void tst_LongshotAnalyzer::hoverChangeDoesNotBreakShift()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.hoverChange = true;
    Trajectory t;
    t.offsets = {880, 910, 940};
    const QImage a = renderFrame(page, kViewport, t, 0, d); // frame 0: colour A
    const QImage b = renderFrame(page, kViewport, t, 1, d); // frame 1: colour B
    const auto obs = LongshotAnalyzer::estimateShift(a, LongshotAnalyzer::computeFeatures(a, 0),
                                                     b, LongshotAnalyzer::computeFeatures(b, 50), 0, 1, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QVERIFY2(qAbs(obs->shift.dy - 30) <= kShiftTolerance, qPrintable(QString::number(obs->shift.dy)));
}

void tst_LongshotAnalyzer::blankBandInTemplateIsNotAMatch()
{
    PageSpec spec;
    spec.blankTop = 1200;
    spec.blankHeight = 300;
    const QImage page = renderPage(spec);
    // Ink above and below the gap: the true shift must still be found.
    const Pair a = makePair(page, 1000, 1040);
    const auto found = LongshotAnalyzer::estimateShift(a.from, a.fromFeatures, a.to, a.toFeatures, 0, 1, AnalyzerParams{});
    QVERIFY(found.has_value());
    QCOMPARE(found->shift.dy, 40);
    // Ink only in rows 1150-1199 and 1500-1629 of the viewport: right or refuse, never wrong.
    const Pair b = makePair(page, 1150, 1190);
    const auto sparse = LongshotAnalyzer::estimateShift(b.from, b.fromFeatures, b.to, b.toFeatures, 0, 1, AnalyzerParams{});
    if (sparse.has_value()) QCOMPARE(sparse->shift.dy, 40);
}

void tst_LongshotAnalyzer::horizontalShiftIsRefused()
{
    // Vertical scroll by 30 plus a 6 px horizontal pan: the engine is
    // vertical-only, so the pair must be refused rather than stitched torn.
    const QImage page = renderPage(PageSpec{});
    const QImage from = page.copy(0, 1000, kViewport.width(), kViewport.height());
    QImage to(kViewport, QImage::Format_RGB32);
    to.fill(Qt::white);
    {
        QPainter painter(&to);
        painter.drawImage(6, 0, page.copy(0, 1030, kViewport.width(), kViewport.height()));
    }
    const auto obs = LongshotAnalyzer::estimateShift(from, LongshotAnalyzer::computeFeatures(from, 0), to,
                                                     LongshotAnalyzer::computeFeatures(to, 50), 0, 1, AnalyzerParams{});
    QVERIFY2(!obs.has_value(), qPrintable(QStringLiteral("accepted dy %1").arg(obs ? obs->shift.dy : 0)));
}

void tst_LongshotAnalyzer::zoomIsRefused()
{
    // The same page window magnified by 1.1 around its centre (a browser zoom).
    const QImage page = renderPage(PageSpec{});
    const QImage from = page.copy(0, 1000, kViewport.width(), kViewport.height());
    const QSize zoomed(qRound(kViewport.width() * 1.1), qRound(kViewport.height() * 1.1));
    const QImage to = from.scaled(zoomed, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                          .copy((zoomed.width() - kViewport.width()) / 2, (zoomed.height() - kViewport.height()) / 2,
                                kViewport.width(), kViewport.height())
                          .convertToFormat(QImage::Format_RGB32);
    const FrameFeatures fromFeatures = LongshotAnalyzer::computeFeatures(from, 0);
    const FrameFeatures toFeatures = LongshotAnalyzer::computeFeatures(to, 50);
    const auto obs = LongshotAnalyzer::estimateShift(from, fromFeatures, to, toFeatures, 0, 1, AnalyzerParams{});
    QVERIFY2(!obs.has_value(), qPrintable(QStringLiteral("accepted dy %1").arg(obs ? obs->shift.dy : 0)));
    // With the NCC thresholds relaxed so the vertical match alone would
    // accept the pair, the scale check still refuses it.
    AnalyzerParams relaxed;
    relaxed.minPeakScore = 0.0;
    relaxed.minMargin = 0.0;
    const auto relaxedObs = LongshotAnalyzer::estimateShift(from, fromFeatures, to, toFeatures, 0, 1, relaxed);
    QVERIFY2(!relaxedObs.has_value(), qPrintable(QStringLiteral("accepted dy %1").arg(relaxedObs ? relaxedObs->shift.dy : 0)));
}

QTEST_MAIN(tst_LongshotAnalyzer)
#include "tst_LongshotAnalyzer.moc"
