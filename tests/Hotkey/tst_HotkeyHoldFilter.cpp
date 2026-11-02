#include <QtTest>

#include "hotkey/HotkeyHoldFilter.h"

using SnapTray::HotkeyHoldFilter;

// A held recording hotkey is one press (PR #165 review): auto-repeat must
// not pause and resume a recording while the key is held.
class TestHotkeyHoldFilter : public QObject
{
    Q_OBJECT

private slots:
    void holdingIsOnePress();
    void quickDoublePressIsIgnored();
    void releaseAllowsTheNextPress();
    void missingReleaseDoesNotLockTheKey();
    void keysAreIndependent();
};

void TestHotkeyHoldFilter::holdingIsOnePress()
{
    HotkeyHoldFilter filter;
    QVERIFY(filter.accept(1, 0));
    // X11 auto-repeat: first repeat after ~500 ms, then every ~30 ms, for 5 s.
    for (qint64 t = 500; t < 5000; t += 30) {
        QVERIFY2(!filter.accept(1, t), qPrintable(QString::number(t)));
    }
}

void TestHotkeyHoldFilter::quickDoublePressIsIgnored()
{
    HotkeyHoldFilter filter;
    QVERIFY(filter.accept(1, 0));
    filter.release(1);
    QVERIFY(!filter.accept(1, HotkeyHoldFilter::kMinIntervalMs - 1));
}

void TestHotkeyHoldFilter::releaseAllowsTheNextPress()
{
    HotkeyHoldFilter filter;
    QVERIFY(filter.accept(1, 0));
    QVERIFY(!filter.accept(1, 600)); // repeat
    filter.release(1);
    QVERIFY(filter.accept(1, 800));  // a real second press
    QVERIFY(!filter.accept(1, 1300)); // its repeat
}

void TestHotkeyHoldFilter::missingReleaseDoesNotLockTheKey()
{
    HotkeyHoldFilter filter;
    QVERIFY(filter.accept(1, 0));
    // No release reported; activations stop, then a press a while later counts.
    QVERIFY(filter.accept(1, HotkeyHoldFilter::kHeldExpiryMs + 1));
}

void TestHotkeyHoldFilter::keysAreIndependent()
{
    HotkeyHoldFilter filter;
    QVERIFY(filter.accept(1, 0));
    QVERIFY(filter.accept(2, 10));
    QVERIFY(!filter.accept(1, 600));
}

QTEST_GUILESS_MAIN(TestHotkeyHoldFilter)
#include "tst_HotkeyHoldFilter.moc"
