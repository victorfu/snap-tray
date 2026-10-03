#include "longshot/PositionSolver.h"

#include <QtTest>

using namespace SnapTray::Longshot;

namespace {

std::vector<qint64> times(int count)
{
    std::vector<qint64> t(count);
    for (int i = 0; i < count; ++i) t[i] = qint64(i) * 100;
    return t;
}

PairShift edge(int from, int to, int dy, double confidence = 1.0)
{
    PairShift e;
    e.from = from;
    e.to = to;
    e.dy = dy;
    e.confidence = confidence;
    return e;
}

} // namespace

class tst_PositionSolver : public QObject
{
    Q_OBJECT
private slots:
    void chainIsCumulative();
    void loopClosureDistributesDrift();
    void outlierEdgeIsRejected();
    void disconnectedIslandsReportBreaks();
    void keepsLargestIsland();
    void roundsToIntegers();
    void emptyInputs();
    void zeroConfidenceEdgeIgnored();
    void longChainConverges();
    void longChainWithClosure();
    void equalWeightOutlierClosureRejected();
    void tieBreakIsOrderIndependent();
};

void tst_PositionSolver::chainIsCumulative()
{
    const auto result = PositionSolver::solve(times(4), {edge(0, 1, 10), edge(1, 2, 20), edge(2, 3, -5)}, 3.0);
    QCOMPARE(result.positions.size(), size_t(4));
    QCOMPARE(*result.positions[0], 0);
    QCOMPARE(*result.positions[1], 10);
    QCOMPARE(*result.positions[2], 30);
    QCOMPARE(*result.positions[3], 25);
    QVERIFY(result.breakTimesMs.empty());
    QCOMPARE(result.rejectedEdges, 0);
}

void tst_PositionSolver::loopClosureDistributesDrift()
{
    // Chain says 0->3 is 30; the closure says 27. Least squares spreads the 3 px
    // disagreement across the four equally-weighted edges instead of putting it
    // all on one.
    const auto result = PositionSolver::solve(
        times(4), {edge(0, 1, 10), edge(1, 2, 10), edge(2, 3, 10), edge(0, 3, 27)}, 5.0);
    QCOMPARE(*result.positions[0], 0);
    QVERIFY(qAbs(*result.positions[3] - 28) <= 1); // 27.75 rounds to 28
    QVERIFY(*result.positions[1] >= 9 && *result.positions[1] <= 10);
    QVERIFY(result.breakTimesMs.empty());
    QCOMPARE(result.rejectedEdges, 0);
}

void tst_PositionSolver::outlierEdgeIsRejected()
{
    // One wildly wrong closure must be dropped, leaving the chain intact.
    const auto result = PositionSolver::solve(
        times(4), {edge(0, 1, 10), edge(1, 2, 10), edge(2, 3, 10), edge(0, 3, 300, 0.5)}, 4.0);
    QCOMPARE(*result.positions[3], 30);
    QCOMPARE(result.rejectedEdges, 1);
    QVERIFY(result.breakTimesMs.empty());
}

void tst_PositionSolver::disconnectedIslandsReportBreaks()
{
    // Frames 0-2 connected, 3-5 connected, nothing between 2 and 3.
    const auto result = PositionSolver::solve(
        times(6), {edge(0, 1, 10), edge(1, 2, 10), edge(3, 4, 10), edge(4, 5, 10)}, 3.0);
    QVERIFY(result.positions[0].has_value());
    QVERIFY(result.positions[2].has_value());
    QVERIFY(!result.positions[3].has_value());
    QVERIFY(!result.positions[5].has_value());
    QCOMPARE(result.breakTimesMs, (std::vector<qint64>{300}));
}

void tst_PositionSolver::keepsLargestIsland()
{
    // Island A = {0,1}, island B = {2,3,4}: B is larger, so B is kept and A is the break.
    const auto result = PositionSolver::solve(
        times(5), {edge(0, 1, 10), edge(2, 3, 10), edge(3, 4, 10)}, 3.0);
    QVERIFY(!result.positions[0].has_value());
    QVERIFY(result.positions[2].has_value());
    QCOMPARE(*result.positions[2], 0); // the kept island is anchored at its first frame
    QCOMPARE(*result.positions[4], 20);
    QCOMPARE(result.breakTimesMs, (std::vector<qint64>{0}));
}

void tst_PositionSolver::roundsToIntegers()
{
    // Two conflicting equally weighted edges 0->1: 10 and 11 -> 10.5 -> rounds half away from zero to 11.
    const auto result = PositionSolver::solve(times(2), {edge(0, 1, 10), edge(0, 1, 11)}, 3.0);
    QCOMPARE(*result.positions[1], 11);
}

void tst_PositionSolver::emptyInputs()
{
    const auto none = PositionSolver::solve({}, {}, 3.0);
    QVERIFY(none.positions.empty());
    QVERIFY(none.breakTimesMs.empty());
    // One frame, no edges: a single island at position 0.
    const auto single = PositionSolver::solve(times(1), {}, 3.0);
    QCOMPARE(single.positions.size(), size_t(1));
    QCOMPARE(*single.positions[0], 0);
    // Frames without edges each form their own island; the first is kept.
    const auto isolated = PositionSolver::solve(times(3), {}, 3.0);
    QVERIFY(isolated.positions[0].has_value());
    QVERIFY(!isolated.positions[1].has_value());
    QCOMPARE(isolated.breakTimesMs, (std::vector<qint64>{100, 200}));
}

void tst_PositionSolver::zeroConfidenceEdgeIgnored()
{
    const auto result = PositionSolver::solve(times(3), {edge(0, 1, 10), edge(1, 2, 10, 0.0)}, 3.0);
    QVERIFY(!result.positions[2].has_value());
    QCOMPARE(result.breakTimesMs, (std::vector<qint64>{200}));
}

void tst_PositionSolver::longChainConverges()
{
    std::vector<qint64> t = times(1000);
    std::vector<PairShift> edges;
    for (int i = 0; i < 999; ++i) edges.push_back(edge(i, i + 1, 10));
    const auto result = PositionSolver::solve(t, edges, 3.0);
    QCOMPARE(*result.positions[999], 9990);
    QCOMPARE(*result.positions[500], 5000);
    QVERIFY(result.breakTimesMs.empty());
    QCOMPARE(result.rejectedEdges, 0);
}

void tst_PositionSolver::longChainWithClosure()
{
    std::vector<qint64> t = times(1000);
    std::vector<PairShift> edges;
    for (int i = 0; i < 999; ++i) edges.push_back(edge(i, i + 1, 10));
    edges.push_back(edge(0, 999, 9990));
    const auto result = PositionSolver::solve(t, edges, 3.0);
    QCOMPARE(*result.positions[999], 9990);
    QCOMPARE(*result.positions[500], 5000);
    QVERIFY(result.breakTimesMs.empty());
    QCOMPARE(result.rejectedEdges, 0);
}

void tst_PositionSolver::equalWeightOutlierClosureRejected()
{
    const auto result = PositionSolver::solve(
        times(4), {edge(0, 1, 10), edge(1, 2, 10), edge(2, 3, 10), edge(0, 3, 300, 1.0)}, 4.0);
    QCOMPARE(*result.positions[3], 30);
    QCOMPARE(result.rejectedEdges, 1);
    QVERIFY(result.breakTimesMs.empty());
}

void tst_PositionSolver::tieBreakIsOrderIndependent()
{
    // Same edges as equalWeightOutlierClosureRejected but closure listed FIRST
    const auto result = PositionSolver::solve(
        times(4), {edge(0, 3, 300, 1.0), edge(0, 1, 10), edge(1, 2, 10), edge(2, 3, 10)}, 4.0);
    QCOMPARE(*result.positions[3], 30);
    QCOMPARE(result.rejectedEdges, 1);
    QVERIFY(result.breakTimesMs.empty());
}

QTEST_APPLESS_MAIN(tst_PositionSolver)
#include "tst_PositionSolver.moc"
