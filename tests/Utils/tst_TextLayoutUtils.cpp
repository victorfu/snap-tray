#include <QtTest/QtTest>

#include "utils/TextLayoutUtils.h"

#include <QRandomGenerator>
#include <QStringList>

#include <algorithm>
#include <vector>

namespace {

constexpr int kCap = 800;

// Measures a line as its character count times a fixed advance, recording how
// many lines were measured.
struct CountingMeasure
{
    int advance = 10;
    int* calls = nullptr;

    int operator()(const QString& line) const
    {
        ++*calls;
        return static_cast<int>(line.size()) * advance;
    }
};

int referenceWidth(const QStringList& lines, int cap, int advance)
{
    int maxWidth = 0;
    for (const QString& line : lines) {
        maxWidth = (std::max)(maxWidth, static_cast<int>(line.size()) * advance);
    }
    return (std::min)(maxWidth, cap);
}

} // namespace

class tst_TextLayoutUtils : public QObject
{
    Q_OBJECT

private slots:
    void boundedMaxLineWidth_emptyInputIsZero();
    void boundedMaxLineWidth_belowCapMeasuresEveryLine();
    void boundedMaxLineWidth_stopsOnceCapReached();
    void boundedMaxLineWidth_exactCapStops();
    void boundedMaxLineWidth_lateLongLineStillWidens();
    void boundedMaxLineWidth_matchesReference();
};

void tst_TextLayoutUtils::boundedMaxLineWidth_emptyInputIsZero()
{
    int calls = 0;
    QCOMPARE(TextLayoutUtils::boundedMaxLineWidth(QStringList{}, kCap, CountingMeasure{10, &calls}), 0);
    QCOMPARE(calls, 0);

    // split('\n') of an empty string yields one empty line.
    calls = 0;
    QCOMPARE(TextLayoutUtils::boundedMaxLineWidth(QString().split('\n'), kCap,
                                                  CountingMeasure{10, &calls}),
             0);
    QCOMPARE(calls, 1);
}

void tst_TextLayoutUtils::boundedMaxLineWidth_belowCapMeasuresEveryLine()
{
    const QStringList lines{QStringLiteral("abc"), QString(), QStringLiteral("abcdefgh"),
                            QStringLiteral("ab")};
    int calls = 0;
    QCOMPARE(TextLayoutUtils::boundedMaxLineWidth(lines, kCap, CountingMeasure{10, &calls}), 80);
    QCOMPARE(calls, lines.size());
}

void tst_TextLayoutUtils::boundedMaxLineWidth_stopsOnceCapReached()
{
    QStringList lines;
    lines << QStringLiteral("short") << QString(100, QLatin1Char('w'));
    for (int i = 0; i < 10000; ++i) {
        lines << QString(200, QLatin1Char('x'));
    }

    int calls = 0;
    QCOMPARE(TextLayoutUtils::boundedMaxLineWidth(lines, kCap, CountingMeasure{10, &calls}), kCap);
    QCOMPARE(calls, 2);
}

void tst_TextLayoutUtils::boundedMaxLineWidth_exactCapStops()
{
    const QStringList lines{QString(kCap / 10, QLatin1Char('a')), QStringLiteral("b")};
    int calls = 0;
    QCOMPARE(TextLayoutUtils::boundedMaxLineWidth(lines, kCap, CountingMeasure{10, &calls}), kCap);
    QCOMPARE(calls, 1);
}

void tst_TextLayoutUtils::boundedMaxLineWidth_lateLongLineStillWidens()
{
    // A long line far below the visible height must still widen the card,
    // matching the previous full scan.
    QStringList lines;
    for (int i = 0; i < 1000; ++i) {
        lines << QStringLiteral("hi");
    }
    lines << QString(50, QLatin1Char('z'));

    int calls = 0;
    QCOMPARE(TextLayoutUtils::boundedMaxLineWidth(lines, kCap, CountingMeasure{10, &calls}), 500);
    QCOMPARE(calls, lines.size());
}

void tst_TextLayoutUtils::boundedMaxLineWidth_matchesReference()
{
    QRandomGenerator rng(293);
    for (int round = 0; round < 500; ++round) {
        QStringList lines;
        const int lineCount = rng.bounded(1, 40);
        for (int i = 0; i < lineCount; ++i) {
            lines << QString(rng.bounded(0, 120), QLatin1Char('m'));
        }
        const int advance = rng.bounded(1, 12);
        int calls = 0;
        QCOMPARE(TextLayoutUtils::boundedMaxLineWidth(lines, kCap, CountingMeasure{advance, &calls}),
                 referenceWidth(lines, kCap, advance));
        QVERIFY(calls <= lines.size());
    }
}

QTEST_GUILESS_MAIN(tst_TextLayoutUtils)
#include "tst_TextLayoutUtils.moc"
