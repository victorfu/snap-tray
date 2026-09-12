#include <QtTest>
#include "capture/FrameRateUpdateState.h"

class TestFrameRateUpdateState : public QObject
{
    Q_OBJECT
private slots:
    void coalescesAndSerializes()
    {
        SnapTray::FrameRateUpdateState state;
        state.reset(15);
        state.request(30);
        const auto first = state.begin();
        QVERIFY(first);
        QCOMPARE(first->fps, 30);
        state.request(10);
        state.request(5);
        QVERIFY(!state.begin());
        QVERIFY(state.complete(*first, true));
        const auto next = state.begin();
        QVERIFY(next);
        QCOMPARE(next->fps, 5);
        QVERIFY(state.complete(*next, true));
        QVERIFY(!state.begin());
    }
    void restartDiscardsLateCompletion()
    {
        SnapTray::FrameRateUpdateState state;
        state.reset(15);
        state.request(30);
        const auto old = state.begin();
        QVERIFY(old);
        state.reset(10);
        state.request(5);
        const auto current = state.begin();
        QVERIFY(current);
        QVERIFY(!state.complete(*old, true));
        QVERIFY(!state.begin());
        QVERIFY(state.complete(*current, true));
        QVERIFY(!state.begin());
    }
    void failureDoesNotCommitRate()
    {
        SnapTray::FrameRateUpdateState state;
        state.reset(15);
        state.request(30);
        const auto update = state.begin();
        QVERIFY(update);
        QVERIFY(state.complete(*update, false));
        state.request(15);
        QVERIFY(!state.begin());
    }
};
QTEST_GUILESS_MAIN(TestFrameRateUpdateState)
#include "tst_FrameRateUpdateState.moc"
