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

QImage viewport(const QImage& doc, int offset)
{
    QImage image(kWidth, kHeight, QImage::Format_ARGB32);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int row = y < kHeader ? 600 + y
                : y - kHeader + (x < kContentWidth ? offset : std::min(offset, 38));
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
};

void tst_ScrollContentAssembler::exportsExactContentAcrossStickyTransition()
{
    const auto doc = document();
    QVector<ScrollContentPair> pairs;
    for (int offset = 0; offset < 120; offset += 30) {
        pairs.append({viewport(doc, offset), viewport(doc, offset + 30), 30});
    }
    const auto result = assembleScrollContent(pairs);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.viewport, QRect(0, kHeader, kContentWidth, kHeight - kHeader));
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
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.image.copy(result.viewport.x(), result.viewport.y(),
                               result.viewport.width(), result.viewport.height() + offset),
             doc.copy(result.viewport.x(), result.viewport.y() - kHeader,
                      result.viewport.width(), result.viewport.height() + offset));
    QCOMPARE(result.image.copy(0, 0, kWidth, result.viewport.bottom() + 1),
             pairs.first().before.copy(0, 0, kWidth, result.viewport.bottom() + 1));
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
    QCOMPARE(assembleScrollContent({}).error, QStringLiteral("invalid_input"));
    const auto doc = document();
    const auto frame = viewport(doc, 0);
    for (int shift : {-30, 0}) {
        const auto result = assembleScrollContent({{frame, frame, shift}});
        QCOMPARE(result.error, QStringLiteral("invalid_geometry_or_shift"));
        QVERIFY(result.image.isNull());
    }
    QCOMPARE(assembleScrollContent({{frame, frame.copy(0, 0, 20, 20), 30}}).error,
             QStringLiteral("invalid_geometry_or_shift"));
    QImage blank(kWidth, kHeight, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    QCOMPARE(assembleScrollContent({{blank, blank, 30}}).error,
             QStringLiteral("insufficient_content_evidence"));
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
    QVERIFY(!assembleScrollContent({{before, after, 30}}).error.isEmpty());
}

void tst_ScrollContentAssembler::respectsPixelBudget()
{
    const auto doc = document();
    const QVector<ScrollContentPair> pairs{{viewport(doc, 0), viewport(doc, 30), 30},
                                         {viewport(doc, 30), viewport(doc, 60), 30},
                                         {viewport(doc, 60), viewport(doc, 90), 30}};
    QCOMPARE(assembleScrollContent(pairs, 1).error, QStringLiteral("pixel_budget_exceeded"));
    QCOMPARE(assembleScrollContent(pairs, kWidth * kHeight).error, QStringLiteral("pixel_budget_exceeded"));
}

QTEST_GUILESS_MAIN(tst_ScrollContentAssembler)
#include "tst_ScrollContentAssembler.moc"
