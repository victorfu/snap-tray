#include <QtTest>
#include <limits>
#include "pinwindow/PinLayoutGeometry.h"

using namespace PinLayoutGeometry;

class TestPinLayoutGeometry : public QObject {
    Q_OBJECT
private slots:
    void mixedDpr() {
        const QVector<Source> sources = {{{400, 200}, 2}, {{150, 300}, 1}};
        Options options;
        options.mode = Mode::Horizontal;
        const auto result = calculate(sources, 2, options);
        QVERIFY(result.valid());
        QCOMPARE(result.placements, QVector<QRect>({{0, 0, 200, 100}, {208, 0, 50, 100}}));
        QCOMPARE(result.physicalSize, QSize(516, 200));
        options.mode = Mode::Vertical;
        const auto vertical = calculate(sources, 2, options);
        QVERIFY(vertical.valid());
        QCOMPARE(vertical.placements, QVector<QRect>({{0, 0, 75, 38}, {0, 46, 75, 150}}));
    }
    void fractionalDpr_data() {
        QTest::addColumn<qreal>("dpr");
        for (qreal dpr : {1.0, 1.25, 1.5, 2.0}) QTest::newRow(qPrintable(QString::number(dpr))) << dpr;
    }
    void fractionalDpr() {
        QFETCH(qreal, dpr);
        Options options;
        options.mode = Mode::Horizontal;
        const QVector<Source> sources = {{{300, 150}, 1.5}, {{200, 300}, 2}};
        const auto result = calculate(sources, dpr, options);
        QVERIFY(result.valid());
        QCOMPARE(result.placements[0].height(), result.placements[1].height());
        for (int i = 0; i < sources.size(); ++i) {
            QVERIFY(qCeil(result.placements[i].width() * dpr) <= sources[i].pixels.width());
            QVERIFY(qCeil(result.placements[i].height() * dpr) <= sources[i].pixels.height());
            QVERIFY(qAbs(result.placements[i].width() - result.placements[i].height()
                * double(sources[i].pixels.width()) / sources[i].pixels.height()) <= 0.5);
        }
    }
    void orderGapAndOriginal() {
        Options options;
        options.preventUpscale = false;
        options.order = {2, 0, 1};
        options.gap = 7;
        options.maxRowWidth = 200;
        const auto result = calculate({{{100, 50}, 1}, {{100, 50}, 1}, {{80, 100}, 1}}, 1, options);
        QVERIFY(result.valid());
        QCOMPARE(result.placements, QVector<QRect>({{87, 0, 100, 50}, {0, 107, 100, 50}, {0, 0, 80, 100}}));
    }
    void rejectUnsafeInputs() {
        Options options;
        QVERIFY(!calculate({}, 1, options).valid());
        QVERIFY(!calculate({{{0, 10}, 1}}, 1, options).valid());
        QVERIFY(!calculate({{{10, 10}, 0}}, 1, options).valid());
        QVERIFY(!calculate({{{10, 10}, 1}}, std::numeric_limits<double>::infinity(), options).valid());
        QVERIFY(!calculate({{{10, 10}, 1}}, std::numeric_limits<double>::min(), options).valid());
        QVERIFY(!calculate(QVector<Source>(21, {{10, 10}, 1}), 1, options).valid());
        QVERIFY(calculate(QVector<Source>(20, {{10, 10}, 1}), 1, options).valid());
        options.gap = std::numeric_limits<int>::max();
        QVERIFY(!calculate({{{10, 10}, 1}, {{10, 10}, 1}}, 1, options).valid());
        options.gap = 0;
        options.preventUpscale = false;
        QVERIFY(!calculate({{{10000, 10000}, 1}}, 2, options).valid());
        QVERIFY(!calculate({{{std::numeric_limits<int>::max(), 10}, 1}}, 1, options).valid());
        options.order = {0, 0};
        QVERIFY(!calculate({{{10, 10}, 1}, {{10, 10}, 1}}, 1, options).valid());
    }
    void physicalBudgetAndRecordedLogicalSizes() {
        QVERIFY(safeOutputSize({8000, 8000}, 1));
        QVERIFY(!safeOutputSize({8001, 8000}, 1));
        QVERIFY(safeOutputSize({4000, 4000}, 2));
        QVERIFY(!safeOutputSize({4001, 4000}, 2));
        QVERIFY(!safeOutputSize({10001, 1}, 1));
        Options options;
        options.preventUpscale = false;
        // PNG strips transient DPR, but existing region logical bounds survive.
        const auto original = calculate({{{300, 150}, 1, {200, 100}}}, 1.5, options);
        QVERIFY(original.valid());
        QCOMPARE(original.logicalSize, QSize(200, 100));
        QCOMPARE(original.physicalSize, QSize(300, 150));
        options.preventUpscale = true;
        const auto fractional = calculate({{{151, 121}, 1.5}}, 1.5, options);
        QVERIFY(fractional.valid());
        QVERIFY(fractional.physicalSize.width() <= 151);
        QVERIFY(fractional.physicalSize.height() <= 121);
    }
};
QTEST_APPLESS_MAIN(TestPinLayoutGeometry)
#include "tst_PinLayoutGeometry.moc"
