#include "scrollcapture/ScrollContentAssembler.h"
#include <QtTest>

using namespace snaptray::scrollcapture;

namespace {
constexpr int kWidth = 120;
constexpr int kHeight = 180;
constexpr int kHeader = 20;
constexpr int kContentWidth = 90;

QImage document()
{
    QImage image(kWidth, 800, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const quint32 value = quint32(y * 2654435761U + x * 40503U);
            image.setPixel(x, y, qRgb(value & 255, (value >> 8) & 255, (value >> 16) & 255));
        }
    }
    return image;
}

QImage viewport(const QImage& doc, int offset, int contentLeft = 0, int footer = 0)
{
    QImage image(kWidth, kHeight, QImage::Format_ARGB32);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const bool contentColumn = x >= contentLeft && x < contentLeft + kContentWidth;
            const int row = y < kHeader || y >= kHeight - footer ? 600 + y
                : y - kHeader + (contentColumn ? offset : std::min(offset, 38));
            image.setPixel(x, y, doc.pixel(x, row));
        }
    }
    return image;
}
}

class tst_ScrollContentAssembler : public QObject
{
    Q_OBJECT
private slots:
    void exportsExactContentAcrossStickyTransition();
    void variableShiftsPreserveEveryRow();
    void rejectsDiscontinuousSession();
    void rejectsInvalidOrBlankInput();
    void rejectsRepeatingContentWithoutUniqueEvidence();
    void rejectsMinorityContent();
    void respectsPixelBudget();
    void geometryVariants_data();
    void geometryVariants();
    void rejectsOffByOneShift();
    void defaultResultIsNotSuccess();
};

void tst_ScrollContentAssembler::exportsExactContentAcrossStickyTransition()
{
    const auto doc = document();
    QVector<ScrollContentPair> pairs;
    for (int offset = 0; offset < 120; offset += 30) {
        pairs.append({viewport(doc, offset), viewport(doc, offset + 30), 30});
    }
    const auto result = assembleScrollContent(pairs);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.contentRect, QRect(0, kHeader, kContentWidth, kHeight - kHeader));
    QCOMPARE(result.image.size(), QSize(kWidth, kHeight + 120));
    QCOMPARE(result.image.copy(0, 0, kWidth, kHeight), pairs.first().before);
    QCOMPARE(result.image.copy(0, kHeader, kContentWidth, kHeight - kHeader + 120),
             doc.copy(0, 0, kContentWidth, kHeight - kHeader + 120));
    // Sidebar is preserved once, never repeated or replaced with invented pixels.
    for (int y = kHeight; y < result.image.height(); ++y) {
        for (int x = kContentWidth; x < kWidth; ++x) QCOMPARE(result.image.pixel(x, y), QRgb(0));
    }
}

void tst_ScrollContentAssembler::variableShiftsPreserveEveryRow()
{
    const auto doc = document();
    QVector<ScrollContentPair> pairs;
    int offset = 0;
    for (int shift : {30, 31, 29, 30}) {
        pairs.append({viewport(doc, offset), viewport(doc, offset + shift), shift});
        offset += shift;
    }
    const auto result = assembleScrollContent(pairs);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.contentRect, QRect(0, kHeader, kContentWidth, kHeight - kHeader));
    QCOMPARE(result.image.size(), QSize(kWidth, kHeight + offset));
    QCOMPARE(result.image.copy(result.contentRect.x(), result.contentRect.y(),
                               result.contentRect.width(), result.contentRect.height() + offset),
             doc.copy(result.contentRect.x(), result.contentRect.y() - kHeader,
                      result.contentRect.width(), result.contentRect.height() + offset));
    QCOMPARE(result.image.copy(0, 0, kWidth, result.contentRect.bottom() + 1),
             pairs.first().before.copy(0, 0, kWidth, result.contentRect.bottom() + 1));
}

void tst_ScrollContentAssembler::rejectsDiscontinuousSession()
{
    const auto doc = document();
    const auto result = assembleScrollContent({{viewport(doc, 0), viewport(doc, 30), 30},
                                              {viewport(doc, 31), viewport(doc, 61), 30}});
    QCOMPARE(result.error, QStringLiteral("discontinuous_capture"));
    QVERIFY(result.image.isNull());
}

void tst_ScrollContentAssembler::rejectsInvalidOrBlankInput()
{
    const auto checkFailure = [](const ScrollContentResult& result, const char* reason) {
        QCOMPARE(result.error, QLatin1String(reason));
        QVERIFY(!result.ok());
        QVERIFY(result.image.isNull());
    };
    checkFailure(assembleScrollContent({}), "invalid_input");
    const auto frame = viewport(document(), 0);
    for (int shift : {-30, 0}) {
        checkFailure(assembleScrollContent({{frame, frame, shift}}), "non_positive_shift");
    }
    for (int shift : {kHeight / 2, kHeight}) {
        checkFailure(assembleScrollContent({{frame, frame, shift}}), "shift_too_large");
    }
    checkFailure(assembleScrollContent({{frame, frame.copy(0, 0, 20, 20), 30}}), "invalid_geometry");
    QImage blank(kWidth, kHeight, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    checkFailure(assembleScrollContent({{blank, blank, 30}}), "insufficient_content_evidence");
}

void tst_ScrollContentAssembler::rejectsRepeatingContentWithoutUniqueEvidence()
{
    // Content repeats every 10 rows and really scrolled by 5. A supplied shift
    // of 15 also overlaps pixel-exactly, so only the unique-row evidence check
    // can refuse to append 10 rows per pair that were never revealed.
    constexpr int kPeriod = 10;
    const auto doc = document();
    const auto periodic = [&](int offset) {
        QImage image(kWidth, kHeight, QImage::Format_ARGB32);
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                const int row = y < kHeader ? 600 + y : (y - kHeader + offset) % kPeriod;
                image.setPixel(x, y, doc.pixel(x, row));
            }
        }
        return image;
    };
    for (int shift : {15, 5}) {
        const auto result = assembleScrollContent({{periodic(0), periodic(5), shift}});
        QCOMPARE(result.error, QStringLiteral("insufficient_content_evidence"));
        QVERIFY(result.image.isNull());
    }
}

void tst_ScrollContentAssembler::rejectsMinorityContent()
{
    const auto doc = document();
    auto before = viewport(doc, 100);
    auto after = viewport(doc, 130);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = kWidth / 3; x < kWidth; ++x) after.setPixel(x, y, before.pixel(x, y));
    }
    const auto result = assembleScrollContent({{before, after, 30}});
    QCOMPARE(result.error, QStringLiteral("no_common_content_region"));
    QVERIFY(result.image.isNull());
}

void tst_ScrollContentAssembler::respectsPixelBudget()
{
    const auto doc = document();
    const QVector<ScrollContentPair> pairs{{viewport(doc, 0), viewport(doc, 30), 30},
                                         {viewport(doc, 30), viewport(doc, 60), 30},
                                         {viewport(doc, 60), viewport(doc, 90), 30}};
    constexpr qint64 exactBudget = kWidth * (kHeight + 90);
    const auto exact = assembleScrollContent(pairs, exactBudget);
    QVERIFY2(exact.ok(), qPrintable(exact.error));
    QCOMPARE(exact.image.size(), QSize(kWidth, kHeight + 90));
    for (qint64 budget : {qint64(1), qint64(kWidth * kHeight), exactBudget - 1}) {
        const auto rejected = assembleScrollContent(pairs, budget);
        QCOMPARE(rejected.error, QStringLiteral("pixel_budget_exceeded"));
        QVERIFY(rejected.image.isNull());
    }
}

void tst_ScrollContentAssembler::geometryVariants_data()
{
    QTest::addColumn<int>("left");
    QTest::addColumn<int>("footer");
    QTest::addColumn<int>("format");
    QTest::newRow("short-final-scroll") << 0 << 0 << int(QImage::Format_ARGB32);
    QTest::newRow("left-sidebar") << kWidth - kContentWidth << 0 << int(QImage::Format_ARGB32);
    QTest::newRow("sticky-footer") << 0 << 20 << int(QImage::Format_ARGB32);
    QTest::newRow("left-footer-rgb888") << kWidth - kContentWidth << 20 << int(QImage::Format_RGB888);
}

void tst_ScrollContentAssembler::geometryVariants()
{
    QFETCH(int, left);
    QFETCH(int, footer);
    QFETCH(int, format);
    const auto doc = document();
    QVector<ScrollContentPair> pairs;
    int offset = 0;
    for (int shift : {80, 80, 10}) {
        pairs.append({viewport(doc, offset, left, footer).convertToFormat(QImage::Format(format)),
                      viewport(doc, offset + shift, left, footer).convertToFormat(QImage::Format(format)), shift});
        offset += shift;
    }
    const auto result = assembleScrollContent(pairs);
    QVERIFY2(result.ok(), qPrintable(result.error));
    const QRect expected(left, kHeader, kContentWidth, kHeight - kHeader - footer);
    QCOMPARE(result.contentRect, expected);
    QCOMPARE(result.image.size(), QSize(kWidth, kHeight - footer + offset));
    QCOMPARE(result.image.copy(left, kHeader, kContentWidth, expected.height() + offset),
             doc.copy(left, 0, kContentWidth, expected.height() + offset));
    QCOMPARE(result.image.copy(0, 0, kWidth, kHeight - footer),
             pairs.first().before.copy(0, 0, kWidth, kHeight - footer).convertToFormat(QImage::Format_ARGB32));
    for (int y = kHeight - footer; y < result.image.height(); ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (x < left || x >= left + kContentWidth) QCOMPARE(result.image.pixel(x, y), QRgb(0));
        }
    }
}

void tst_ScrollContentAssembler::rejectsOffByOneShift()
{
    const auto doc = document();
    const auto result = assembleScrollContent({{viewport(doc, 0), viewport(doc, 31), 30}});
    QCOMPARE(result.error, QStringLiteral("no_common_content_region"));
    QVERIFY(result.image.isNull());
}

void tst_ScrollContentAssembler::defaultResultIsNotSuccess()
{
    QVERIFY(!ScrollContentResult{}.ok());
}

QTEST_GUILESS_MAIN(tst_ScrollContentAssembler)
#include "tst_ScrollContentAssembler.moc"
