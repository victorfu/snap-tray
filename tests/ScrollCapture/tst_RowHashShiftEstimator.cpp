#include "scrollcapture/FrameRowHashes.h"
#include "scrollcapture/RowHashShiftEstimator.h"

#include <QImage>
#include <QtTest>

#include <algorithm>
#include <cstring>

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

void tst_RowHashShiftEstimator::findsScrollTowardBottom()
{
    const QImage document = makeDocument();
    const auto result = estimateVerticalShift(computeRowHashes(viewportAt(document, 100)),
                                              computeRowHashes(viewportAt(document, 137)));
    QCOMPARE(result.status, ShiftEstimate::Status::Found);
    QCOMPARE(result.shift, 37);
    QCOMPARE(result.matchedRows, result.overlapRows);
    QCOMPARE(result.overlapRows, kViewportHeight - 37);
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

QTEST_GUILESS_MAIN(tst_RowHashShiftEstimator)
#include "tst_RowHashShiftEstimator.moc"
