#include "scrollcapture/FrameRowHashes.h"
#include "scrollcapture/RowHashShiftEstimator.h"

#include <QImage>
#include <QtTest>

#include <algorithm>
#include <cstring>
#include <limits>

using namespace snaptray::scrollcapture;

namespace {

constexpr int kWidth = 64;
constexpr int kViewportHeight = 200;
constexpr int kDocumentHeight = 1200;

QRgb rowColor(int row, int salt)
{
    const quint32 seed = static_cast<quint32>(row) * 2654435761U + static_cast<quint32>(salt) * 40503U;
    return qRgb(seed & 0xFF, (seed >> 8) & 0xFF, (seed >> 16) & 0xFF);
}

// A document whose every row has its own colour plus a column marker, so each
// row is unique.
QImage makeDocument(int height = kDocumentHeight, int salt = 1)
{
    QImage image(kWidth, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < kWidth; ++x) {
            line[x] = x == (y % kWidth) ? qRgb(0, 0, 0) : rowColor(y, salt);
        }
    }
    return image;
}

QImage viewportAt(const QImage& document, int offset, int height = kViewportHeight)
{
    return document.copy(0, offset, document.width(), height);
}

void paintRows(QImage& image, int top, int rows, const QImage& source, int sourceTop)
{
    for (int y = 0; y < rows; ++y) {
        std::memcpy(image.scanLine(top + y), source.constScanLine(sourceTop + y),
                    static_cast<size_t>(image.bytesPerLine()));
    }
}

} // namespace

class tst_RowHashShiftEstimator : public QObject
{
    Q_OBJECT

private slots:
    void rowHashesIgnoreScanlinePadding();
    void rowHashesConvertNon32BitFormats();
    void fixedViewportBorderDoesNotInvalidateInteriorShift();
    void rowHashInsetRejectsEmptyInterior();
    void findsScrollTowardBottom();
    void findsScrollTowardTop();
    void identicalFramesAreNoMotion();
    void expectedShiftLimitsSearch();
    void expectedShiftOutsideWindowIsNotFound();
    void stickyHeaderDoesNotPinShiftToZero();
    void blankContentLacksDetail();
    void repeatingContentLacksDetail();
    void twoPanesMovingDifferentlyAreAmbiguous();
    void unrelatedFramesAreNotFound();
    void mismatchedHeightsAreInvalid();
    void bandConsensus_data();
    void bandConsensus();
    void bandsDoNotPromoteBlankOrMinorityEvidence();
    void blankMarginsBesideMajorityAreUniform();
    void bandsReportInsufficientDetailForSparseContent();
    void unresolvedBandRejectsAlignment();
    void bandsHonorHorizontalInset();
};

void tst_RowHashShiftEstimator::rowHashesIgnoreScanlinePadding()
{
    // 3-pixel RGB888 rows are padded to 12 bytes; conversion drops padding,
    // and two images with the same pixels must hash the same.
    QImage first(3, 2, QImage::Format_RGB888);
    first.fill(Qt::red);
    QImage second = first.copy();
    QCOMPARE(computeRowHashes(first), computeRowHashes(second));
    QCOMPARE(computeRowHashes(first).size(), 2);
}

void tst_RowHashShiftEstimator::rowHashesConvertNon32BitFormats()
{
    const QImage rgb32 = makeDocument(40);
    const QImage rgb888 = rgb32.convertToFormat(QImage::Format_RGB888);
    QCOMPARE(computeRowHashes(rgb888), computeRowHashes(rgb32));
    QVERIFY(computeRowHashes(QImage()).isEmpty());
}

void tst_RowHashShiftEstimator::fixedViewportBorderDoesNotInvalidateInteriorShift()
{
    const QImage document = makeDocument();
    QImage before = viewportAt(document, 100);
    QImage after = viewportAt(document, 137);
    constexpr int borderWidth = 2;
    // A fixed vertical window-frame gradient remains at viewport coordinates.
    // Full-row hashes cannot match it at a nonzero displacement, even though
    // every interior pixel scrolled exactly as expected.
    for (int y = 0; y < before.height(); ++y) {
        for (int x = 0; x < borderWidth; ++x) {
            const QRgb border = rowColor(y, 7);
            before.setPixel(x, y, border);
            after.setPixel(x, y, border);
            before.setPixel(before.width() - 1 - x, y, border);
            after.setPixel(after.width() - 1 - x, y, border);
        }
    }
    QCOMPARE(estimateVerticalShift(computeRowHashes(before), computeRowHashes(after)).status,
             ShiftEstimate::Status::NotFound);
    ShiftEstimatorOptions options;
    options.expectedShift = 37;
    options.searchRadius = 8;
    const auto result = estimateVerticalShift(computeRowHashes(before, borderWidth),
                                              computeRowHashes(after, borderWidth), options);
    QCOMPARE(result.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.shift, 37);
    QCOMPARE(result.matchedRows, result.overlapRows);
    QCOMPARE(computeRowHashes(before, borderWidth),
             computeRowHashes(before.copy(borderWidth, 0, before.width() - 2 * borderWidth, before.height())));

    // Border exclusion must not turn detail-free interiors into usable motion.
    QImage blank = before;
    blank.fill(Qt::white);
    QImage changed = blank;
    changed.fill(Qt::black);
    QCOMPARE(estimateVerticalShift(computeRowHashes(blank, borderWidth),
                                  computeRowHashes(changed, borderWidth)).status,
             ShiftEstimate::Status::InsufficientDetail);
}

void tst_RowHashShiftEstimator::rowHashInsetRejectsEmptyInterior()
{
    const QImage frame = makeDocument();
    QVERIFY(computeRowHashes(frame, -1).isEmpty());
    QVERIFY(computeRowHashes(frame, frame.width() / 2).isEmpty());
    QVERIFY(computeRowHashes(frame, std::numeric_limits<int>::max()).isEmpty());
    QVERIFY(computeRowHashes(QImage(), 2).isEmpty());
}

void tst_RowHashShiftEstimator::findsScrollTowardBottom()
{
    const QImage document = makeDocument();
    const auto result = estimateVerticalShift(computeRowHashes(viewportAt(document, 100)),
                                              computeRowHashes(viewportAt(document, 137)));
    QCOMPARE(result.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.shift, 37);
    QCOMPARE(result.matchedRows, result.overlapRows);
    QCOMPARE(result.overlapRows, kViewportHeight - 37);
    // Unique rows agree on one shift only, and the whole range was searched.
    QVERIFY(!result.runnerUpShift.has_value());
    QCOMPARE(result.searchLow, -(kViewportHeight - 16));
    QCOMPARE(result.searchHigh, kViewportHeight - 16);
}

void tst_RowHashShiftEstimator::findsScrollTowardTop()
{
    const QImage document = makeDocument();
    const auto result = estimateVerticalShift(computeRowHashes(viewportAt(document, 300)),
                                              computeRowHashes(viewportAt(document, 280)));
    QCOMPARE(result.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.shift, -20);
}

void tst_RowHashShiftEstimator::identicalFramesAreNoMotion()
{
    const auto hashes = computeRowHashes(viewportAt(makeDocument(), 50));
    const auto result = estimateVerticalShift(hashes, hashes);
    QCOMPARE(result.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.shift, 0);
    QCOMPARE(result.staticRows, kViewportHeight);
}

void tst_RowHashShiftEstimator::expectedShiftLimitsSearch()
{
    const QImage document = makeDocument();
    ShiftEstimatorOptions options;
    options.expectedShift = 40;
    options.searchRadius = 8;
    const auto result = estimateVerticalShift(computeRowHashes(viewportAt(document, 0)),
                                              computeRowHashes(viewportAt(document, 37)), options);
    QCOMPARE(result.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.shift, 37);
    QCOMPARE(result.searchLow, 32);
    QCOMPARE(result.searchHigh, 48);
}

void tst_RowHashShiftEstimator::expectedShiftOutsideWindowIsNotFound()
{
    const QImage document = makeDocument();
    ShiftEstimatorOptions options;
    options.expectedShift = 100;
    options.searchRadius = 8;
    const auto result = estimateVerticalShift(computeRowHashes(viewportAt(document, 0)),
                                              computeRowHashes(viewportAt(document, 37)), options);
    QCOMPARE(result.status, ShiftEstimate::Status::NotFound);
    QCOMPARE(result.searchLow, 92);
    QCOMPARE(result.searchHigh, 108);
    QCOMPARE(result.distinctMatches, 0);
}

void tst_RowHashShiftEstimator::stickyHeaderDoesNotPinShiftToZero()
{
    // A 120-row sticky header stays put while 80 rows below it scroll by 30.
    // The header outvotes nothing because static rows do not vote.
    constexpr int kHeaderRows = 120;
    const QImage header = makeDocument(kHeaderRows, 7);
    const QImage document = makeDocument();
    QImage before(kWidth, kViewportHeight, QImage::Format_RGB32);
    QImage after(kWidth, kViewportHeight, QImage::Format_RGB32);
    paintRows(before, 0, kHeaderRows, header, 0);
    paintRows(after, 0, kHeaderRows, header, 0);
    paintRows(before, kHeaderRows, kViewportHeight - kHeaderRows, document, 400);
    paintRows(after, kHeaderRows, kViewportHeight - kHeaderRows, document, 430);

    const auto result = estimateVerticalShift(computeRowHashes(before), computeRowHashes(after));
    QCOMPARE(result.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.shift, 30);
    QCOMPARE(result.staticRows, kHeaderRows);
}

void tst_RowHashShiftEstimator::blankContentLacksDetail()
{
    QImage blank(kWidth, kViewportHeight, QImage::Format_RGB32);
    blank.fill(Qt::white);
    QImage withLine = blank.copy();
    withLine.setPixel(3, 10, qRgb(0, 0, 0));
    const auto result = estimateVerticalShift(computeRowHashes(blank), computeRowHashes(withLine));
    QCOMPARE(result.status, ShiftEstimate::Status::InsufficientDetail);
}

void tst_RowHashShiftEstimator::repeatingContentLacksDetail()
{
    // Stripes with a 10-row period match at every multiple of 10.
    QImage stripes(kWidth, kViewportHeight + 10, QImage::Format_RGB32);
    for (int y = 0; y < stripes.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(stripes.scanLine(y));
        std::fill(line, line + kWidth, rowColor(y % 10, 3));
    }
    const auto result = estimateVerticalShift(computeRowHashes(viewportAt(stripes, 0)),
                                              computeRowHashes(viewportAt(stripes, 3)));
    QCOMPARE(result.status, ShiftEstimate::Status::InsufficientDetail);
}

void tst_RowHashShiftEstimator::twoPanesMovingDifferentlyAreAmbiguous()
{
    // The top half scrolled down by 10 while the bottom half scrolled up by
    // 40, so both shifts get the same number of votes.
    const QImage document = makeDocument();
    constexpr int kHalf = kViewportHeight / 2;
    const QImage before = viewportAt(document, 300);
    QImage after(kWidth, kViewportHeight, QImage::Format_RGB32);
    paintRows(after, 0, kHalf, document, 300 + 10);
    paintRows(after, kHalf, kHalf, document, 300 + kHalf - 40);

    ShiftEstimatorOptions options;
    options.minMatchRatio = 0.3;
    const auto result = estimateVerticalShift(computeRowHashes(before), computeRowHashes(after), options);
    QCOMPARE(result.status, ShiftEstimate::Status::Ambiguous);
    QVERIFY(result.candidates.contains(10));
    QVERIFY(result.candidates.contains(-40));
    // The runner-up is the other pane, with as many votes as the winner.
    QVERIFY(result.runnerUpShift.has_value());
    QCOMPARE(QSet<int>({result.shift, *result.runnerUpShift}), QSet<int>({10, -40}));
    QCOMPARE(result.runnerUpVotes, result.distinctMatches);
    const auto insetResult = estimateVerticalShift(computeRowHashes(before, 2),
                                                   computeRowHashes(after, 2), options);
    QCOMPARE(insetResult.status, ShiftEstimate::Status::Ambiguous);
    QVERIFY(insetResult.candidates.contains(10));
    QVERIFY(insetResult.candidates.contains(-40));
}

void tst_RowHashShiftEstimator::unrelatedFramesAreNotFound()
{
    const auto result = estimateVerticalShift(computeRowHashes(viewportAt(makeDocument(kDocumentHeight, 1), 0)),
                                              computeRowHashes(viewportAt(makeDocument(kDocumentHeight, 2), 0)));
    QCOMPARE(result.status, ShiftEstimate::Status::NotFound);
}

void tst_RowHashShiftEstimator::mismatchedHeightsAreInvalid()
{
    const QImage document = makeDocument();
    const auto result = estimateVerticalShift(computeRowHashes(viewportAt(document, 0, 100)),
                                              computeRowHashes(viewportAt(document, 0, 120)));
    QCOMPARE(result.status, ShiftEstimate::Status::InvalidInput);
    QCOMPARE(estimateVerticalShift({}, {}).status, ShiftEstimate::Status::InvalidInput);
}

void tst_RowHashShiftEstimator::bandConsensus_data()
{
    QTest::addColumn<int>("mainBands");
    QTest::addColumn<int>("sidebarShift");
    QTest::addColumn<int>("expectedStatus");
    QTest::newRow("uniform") << 8 << 37 << int(ShiftEstimate::Status::Found);
    QTest::newRow("sticky-sidebar") << 6 << 0 << int(ShiftEstimate::Status::Found);
    QTest::newRow("different-sidebar") << 5 << 29 << int(ShiftEstimate::Status::Found);
    QTest::newRow("sidebar-reaching-sticky-limit") << 5 << 12 << int(ShiftEstimate::Status::Found);
    QTest::newRow("equal-panes") << 4 << 29 << int(ShiftEstimate::Status::Ambiguous);
}

void tst_RowHashShiftEstimator::bandConsensus()
{
    QFETCH(int, mainBands);
    QFETCH(int, sidebarShift);
    QFETCH(int, expectedStatus);
    const auto document = makeDocument();
    const auto before = viewportAt(document, 100);
    auto after = viewportAt(document, 137);
    for (int y = 0; y < after.height(); ++y) {
        for (int x = mainBands * kWidth / 8; x < kWidth; ++x) {
            after.setPixel(x, y, document.pixel(x, 100 + sidebarShift + y));
        }
    }
    const auto result = estimateVerticalShiftByBands(before, after);
    QCOMPARE(int(result.estimate.status), expectedStatus);
    QCOMPARE(result.nonUniformMotion, mainBands != 8);
    QCOMPARE(result.alignmentEstimate.status, mainBands == 8
        ? ShiftEstimate::Status::Found : (mainBands >= 5
            ? ShiftEstimate::Status::NonUniformMotion : ShiftEstimate::Status::Ambiguous));
    // A narrow prior must not hide a sidebar's independently measured motion.
    ShiftEstimatorOptions narrowPrior;
    narrowPrior.expectedShift = 37;
    narrowPrior.searchRadius = 1;
    const auto narrow = estimateVerticalShiftByBands(before, after, narrowPrior);
    if (mainBands >= 5 && mainBands < 8) {
        QCOMPARE(narrow.alignmentEstimate.status, ShiftEstimate::Status::NonUniformMotion);
        QVERIFY(!narrow.conflictingBands.isEmpty());
        QCOMPARE(narrow.observedBands.size(), 8);
        for (int index : narrow.conflictingBands) {
            QCOMPARE(narrow.observedBands[index].status, ShiftEstimate::Status::Found);
            QCOMPARE(narrow.observedBands[index].shift, sidebarShift);
        }
        QCOMPARE(QString::fromLatin1(shiftEstimateStatusName(narrow.alignmentEstimate.status)),
                 QStringLiteral("non_uniform_motion"));
    }
    if (result.estimate.status == ShiftEstimate::Status::Found) {
        QCOMPARE(result.estimate.shift, 37);
        QCOMPARE(result.supportingBands, mainBands);
    }
    ShiftEstimatorOptions wrongPrior;
    wrongPrior.expectedShift = 100;
    wrongPrior.searchRadius = 8;
    QVERIFY(estimateVerticalShiftByBands(before, after, wrongPrior).estimate.status != ShiftEstimate::Status::Found);
    QCOMPARE(estimateVerticalShiftByBands({}, after).estimate.status, ShiftEstimate::Status::InvalidInput);
    QCOMPARE(estimateVerticalShiftByBands(before, after, {}, kWidth / 2).estimate.status,
             ShiftEstimate::Status::InvalidInput);
}

void tst_RowHashShiftEstimator::bandsDoNotPromoteBlankOrMinorityEvidence()
{
    const auto document = makeDocument();
    auto before = viewportAt(document, 100);
    auto after = viewportAt(document, 137);
    // Only three textured bands move; five blank bands cannot claim zero or
    // lower the majority threshold to three out of three informative bands.
    for (int y = 0; y < before.height(); ++y) {
        for (int x = 3 * kWidth / 8; x < kWidth; ++x) {
            before.setPixel(x, y, qRgb(255, 255, 255));
            after.setPixel(x, y, qRgb(255, 255, 255));
        }
    }
    const auto minority = estimateVerticalShiftByBands(before, after);
    // Blank margins are not a second displacement: one region moved, without
    // a majority. That is neither ambiguous nor non-uniform motion.
    QCOMPARE(minority.estimate.status, ShiftEstimate::Status::NotFound);
    QCOMPARE(minority.alignmentEstimate.status, ShiftEstimate::Status::NotFound);
    QVERIFY(minority.estimate.candidates.isEmpty());
    QVERIFY(!minority.nonUniformMotion);
    const auto unchanged = estimateVerticalShiftByBands(before, before);
    QCOMPARE(unchanged.estimate.status, ShiftEstimate::Status::Found);
    QCOMPARE(unchanged.estimate.shift, 0);
    const auto unrelated = estimateVerticalShiftByBands(viewportAt(document, 100),
                                                       viewportAt(makeDocument(kDocumentHeight, 7), 100));
    QVERIFY(unrelated.estimate.status != ShiftEstimate::Status::Found);
}

void tst_RowHashShiftEstimator::blankMarginsBesideMajorityAreUniform()
{
    const auto document = makeDocument();
    auto before = viewportAt(document, 100);
    auto after = viewportAt(document, 137);
    // Six textured bands move together; two blank margin bands are unchanged.
    for (int y = 0; y < before.height(); ++y) {
        for (int x = 6 * kWidth / 8; x < kWidth; ++x) {
            before.setPixel(x, y, qRgb(255, 255, 255));
            after.setPixel(x, y, qRgb(255, 255, 255));
        }
    }
    const auto result = estimateVerticalShiftByBands(before, after);
    QCOMPARE(result.estimate.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.estimate.shift, 37);
    QCOMPARE(result.supportingBands, 6);
    QCOMPARE(result.alignmentEstimate.status, ShiftEstimate::Status::Found);
    QVERIFY(result.conflictingBands.isEmpty());
    QVERIFY(result.unresolvedBands.isEmpty());
    QVERIFY(!result.nonUniformMotion);
}

void tst_RowHashShiftEstimator::bandsReportInsufficientDetailForSparseContent()
{
    // A blank page with three moving rules: every band sees motion but has too
    // few unique rows to measure it. That is missing detail, not a mismatch.
    QImage before(kWidth, kViewportHeight, QImage::Format_RGB32);
    before.fill(Qt::white);
    QImage after = before;
    for (int rule = 0; rule < 3; ++rule) {
        for (int x = 0; x < kWidth; ++x) {
            before.setPixel(x, 100 + rule * 10, rowColor(rule, 3));
            after.setPixel(x, 63 + rule * 10, rowColor(rule, 3));
        }
    }
    const auto result = estimateVerticalShiftByBands(before, after);
    QCOMPARE(result.estimate.status, ShiftEstimate::Status::InsufficientDetail);
    QCOMPARE(result.alignmentEstimate.status, ShiftEstimate::Status::InsufficientDetail);
    QVERIFY(!result.nonUniformMotion);
}

void tst_RowHashShiftEstimator::unresolvedBandRejectsAlignment()
{
    const auto document = makeDocument();
    const auto before = viewportAt(document, 100);
    auto after = viewportAt(document, 137);
    const auto other = makeDocument(kDocumentHeight, 7);
    for (int y = 0; y < kViewportHeight; ++y) {
        for (int x = 7 * kWidth / 8; x < kWidth; ++x) after.setPixel(x, y, other.pixel(x, y));
    }
    const auto result = estimateVerticalShiftByBands(before, after);
    QCOMPARE(result.estimate.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.estimate.shift, 37);
    QCOMPARE(result.alignmentEstimate.status, ShiftEstimate::Status::NotFound);
    QCOMPARE(result.unresolvedBands, QVector<int>{7});
    QVERIFY(result.conflictingBands.isEmpty());
    QVERIFY(!result.nonUniformMotion);
}

void tst_RowHashShiftEstimator::bandsHonorHorizontalInset()
{
    const auto document = makeDocument();
    auto before = viewportAt(document, 100);
    auto after = viewportAt(document, 137);
    constexpr int inset = 2;
    for (int y = 0; y < kViewportHeight; ++y) {
        for (int x : {0, 1, kWidth - 2, kWidth - 1}) {
            before.setPixel(x, y, rowColor(y, 7));
            after.setPixel(x, y, rowColor(y, 7));
        }
    }
    QCOMPARE(estimateVerticalShiftByBands(before, after).alignmentEstimate.status, ShiftEstimate::Status::NotFound);
    const auto result = estimateVerticalShiftByBands(before, after, {}, inset);
    QCOMPARE(result.estimate.shift, 37);
    QCOMPARE(result.supportingBands, 8);
    QCOMPARE(result.alignmentEstimate.status, ShiftEstimate::Status::Found);
    QVERIFY(result.unresolvedBands.isEmpty());
}

QTEST_GUILESS_MAIN(tst_RowHashShiftEstimator)
#include "tst_RowHashShiftEstimator.moc"
