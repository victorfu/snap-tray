#include <QtTest>
#include "region/UpdateThrottler.h"

class tst_UpdateThrottler : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Basic functionality tests
    void testStartAll();

    // Throttle type tests
    void testSelectionThrottle();
    void testAnnotationThrottle();
    void testHoverThrottle();
    void testMagnifierThrottle();

    // Reset tests
    void testResetSelection();
    void testResetAnnotation();
    void testResetHover();
    void testResetMagnifier();

    // Timing tests
    void testThrottleIntervalsAreCorrect();
    void testDifferentTypesAreIndependent();

    // Constants tests
    void testConstantsAreReasonable();

private:
    UpdateThrottler* m_throttler;
};

void tst_UpdateThrottler::initTestCase()
{
}

void tst_UpdateThrottler::cleanupTestCase()
{
}

void tst_UpdateThrottler::init()
{
    m_throttler = new UpdateThrottler();
}

void tst_UpdateThrottler::cleanup()
{
    delete m_throttler;
    m_throttler = nullptr;
}

void tst_UpdateThrottler::testStartAll()
{
    m_throttler->startAll();

    // Immediately after starting, elapsed time is 0, which is less than threshold
    // So shouldUpdate should return false (throttled)
    QVERIFY(!m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Selection));
    QVERIFY(!m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Annotation));
    QVERIFY(!m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Hover));
    QVERIFY(!m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Magnifier));

    // After waiting for the longest threshold, all should allow update
    QTest::qWait(UpdateThrottler::kHoverMs + 5);
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Selection));
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Annotation));
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Hover));
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Magnifier));
}

void tst_UpdateThrottler::testSelectionThrottle()
{
    m_throttler->startAll();

    // Wait for threshold to pass so we can test the full cycle
    QTest::qWait(UpdateThrottler::kSelectionMs + 5);

    // Should allow update after threshold
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Selection));

    // Reset and check immediately - should not allow update
    m_throttler->reset(UpdateThrottler::ThrottleType::Selection);
    QVERIFY(!m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Selection));

    // Wait for threshold to pass again
    QTest::qWait(UpdateThrottler::kSelectionMs + 5);
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Selection));
}

void tst_UpdateThrottler::testAnnotationThrottle()
{
    m_throttler->startAll();

    // Wait for threshold to pass
    QTest::qWait(UpdateThrottler::kAnnotationMs + 5);

    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Annotation));

    m_throttler->reset(UpdateThrottler::ThrottleType::Annotation);
    QVERIFY(!m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Annotation));

    // Wait for threshold to pass again
    QTest::qWait(UpdateThrottler::kAnnotationMs + 5);
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Annotation));
}

void tst_UpdateThrottler::testHoverThrottle()
{
    m_throttler->startAll();

    // Wait for threshold to pass
    QTest::qWait(UpdateThrottler::kHoverMs + 5);

    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Hover));

    m_throttler->reset(UpdateThrottler::ThrottleType::Hover);

    // Hover has longer interval (32ms), so should be throttled after reset
    bool shouldUpdate = m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Hover);
    QVERIFY(!shouldUpdate);  // Should be throttled immediately after reset
}

void tst_UpdateThrottler::testMagnifierThrottle()
{
    m_throttler->startAll();

    // Wait for threshold to pass
    QTest::qWait(UpdateThrottler::kMagnifierMs + 5);

    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Magnifier));

    m_throttler->reset(UpdateThrottler::ThrottleType::Magnifier);
    bool shouldUpdate = m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Magnifier);
    QVERIFY(!shouldUpdate);  // Should be throttled immediately after reset
}

void tst_UpdateThrottler::testResetSelection()
{
    m_throttler->startAll();

    m_throttler->reset(UpdateThrottler::ThrottleType::Selection);

    // Wait for throttle period to pass
    QTest::qWait(UpdateThrottler::kSelectionMs + 5);

    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Selection));
}

void tst_UpdateThrottler::testResetAnnotation()
{
    m_throttler->startAll();

    m_throttler->reset(UpdateThrottler::ThrottleType::Annotation);

    // Wait for throttle period to pass
    QTest::qWait(UpdateThrottler::kAnnotationMs + 5);

    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Annotation));
}

void tst_UpdateThrottler::testResetHover()
{
    m_throttler->startAll();

    m_throttler->reset(UpdateThrottler::ThrottleType::Hover);

    // Wait for throttle period to pass
    QTest::qWait(UpdateThrottler::kHoverMs + 5);

    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Hover));
}

void tst_UpdateThrottler::testResetMagnifier()
{
    m_throttler->startAll();

    m_throttler->reset(UpdateThrottler::ThrottleType::Magnifier);

    // Wait for throttle period to pass
    QTest::qWait(UpdateThrottler::kMagnifierMs + 5);

    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Magnifier));
}

void tst_UpdateThrottler::testThrottleIntervalsAreCorrect()
{
    // Verify the intervals match the documented values
    QCOMPARE(UpdateThrottler::kSelectionMs, 8);    // 120fps
    QCOMPARE(UpdateThrottler::kAnnotationMs, 12);  // ~80fps
    QCOMPARE(UpdateThrottler::kHoverMs, 32);       // ~30fps
    QCOMPARE(UpdateThrottler::kMagnifierMs, 16);   // ~60fps
}

void tst_UpdateThrottler::testDifferentTypesAreIndependent()
{
    m_throttler->startAll();

    // Wait for all thresholds to pass
    QTest::qWait(UpdateThrottler::kHoverMs + 5);

    // Reset only Selection
    m_throttler->reset(UpdateThrottler::ThrottleType::Selection);

    // Selection should be throttled (just reset)
    QVERIFY(!m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Selection));

    // Others should still be updatable (not reset)
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Annotation));
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Hover));
    QVERIFY(m_throttler->shouldUpdate(UpdateThrottler::ThrottleType::Magnifier));
}

void tst_UpdateThrottler::testConstantsAreReasonable()
{
    // Selection should be fastest (highest fps)
    QVERIFY(UpdateThrottler::kSelectionMs <= UpdateThrottler::kAnnotationMs);
    QVERIFY(UpdateThrottler::kAnnotationMs <= UpdateThrottler::kMagnifierMs);
    QVERIFY(UpdateThrottler::kMagnifierMs <= UpdateThrottler::kHoverMs);

    // All should be positive
    QVERIFY(UpdateThrottler::kSelectionMs > 0);
    QVERIFY(UpdateThrottler::kAnnotationMs > 0);
    QVERIFY(UpdateThrottler::kHoverMs > 0);
    QVERIFY(UpdateThrottler::kMagnifierMs > 0);

    // All should be reasonable (less than 100ms)
    QVERIFY(UpdateThrottler::kSelectionMs < 100);
    QVERIFY(UpdateThrottler::kAnnotationMs < 100);
    QVERIFY(UpdateThrottler::kHoverMs < 100);
    QVERIFY(UpdateThrottler::kMagnifierMs < 100);
}

QTEST_MAIN(tst_UpdateThrottler)
#include "tst_UpdateThrottler.moc"
