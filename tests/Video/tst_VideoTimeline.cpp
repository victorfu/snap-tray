#include <QtTest/QtTest>
#include <QSignalSpy>
#include "video/VideoTimeline.h"

/**
 * @brief Tests for VideoTimeline widget
 *
 * Covers:
 * - Duration and position management
 * - Position/X coordinate conversions
 * - Time formatting
 * - Signal emission (seekRequested, scrubbing)
 * - Scrubbing state management
 * - Mouse interaction
 * - Size hints
 */
class TestVideoTimeline : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Duration tests
    void testSetDuration_UpdatesValue();
    void testSetDuration_ZeroDuration();
    void testSetDuration_LargeDuration();
    void testDuration_DefaultValue();

    // Position tests
    void testSetPosition_UpdatesValue();
    void testSetPosition_ClampsToDuration();
    void testSetPosition_ClampsToZero();
    void testSetPosition_IgnoredWhileScrubbing();
    void testPosition_DefaultValue();

    // Position/X conversion tests
    void testPositionFromX_AtStart();
    void testPositionFromX_AtEnd();
    void testPositionFromX_Middle();
    void testPositionFromX_ZeroDuration();
    void testPositionFromX_ClampsToTrack();

    void testXFromPosition_AtStart();
    void testXFromPosition_AtEnd();
    void testXFromPosition_Middle();
    void testXFromPosition_ZeroDuration();

    // Time formatting tests
    void testFormatTime_Seconds();
    void testFormatTime_MinutesSeconds();
    void testFormatTime_Zero();
    void testFormatTime_Milliseconds();

    // Scrubbing state tests
    void testIsScrubbing_DefaultFalse();
    void testScrubbing_StateChanges();

    // Size hint tests
    void testSizeHint_ReturnsValid();
    void testMinimumSizeHint_ReturnsValid();

    // Signal tests
    void testSeekRequested_EmittedOnClick();
    void testSeekRequested_EmittedOnDrag();
    void testScrubbingStarted_EmittedOnPress();
    void testScrubbingEnded_EmittedOnRelease();

    // Edge cases
    void testEdgeCase_SetPositionBeforeDuration();
    void testEdgeCase_RapidPositionChanges();

private:
    VideoTimeline* m_timeline = nullptr;

    // Helper to access protected methods
    class TestableTimeline : public VideoTimeline {
    public:
        using VideoTimeline::VideoTimeline;
        using VideoTimeline::positionFromX;
        using VideoTimeline::xFromPosition;
        using VideoTimeline::formatTime;
        using VideoTimeline::trackRect;
        using VideoTimeline::playheadRect;
        using VideoTimeline::sizeHint;
        using VideoTimeline::minimumSizeHint;
    };

    TestableTimeline* testable() { return static_cast<TestableTimeline*>(m_timeline); }
};

void TestVideoTimeline::init()
{
    m_timeline = new TestableTimeline();
    m_timeline->resize(400, 24);  // Standard size
}

void TestVideoTimeline::cleanup()
{
    delete m_timeline;
    m_timeline = nullptr;
}

// ============================================================================
// Duration Tests
// ============================================================================

void TestVideoTimeline::testSetDuration_UpdatesValue()
{
    m_timeline->setDuration(10000);  // 10 seconds
    QCOMPARE(m_timeline->duration(), qint64(10000));
}

void TestVideoTimeline::testSetDuration_ZeroDuration()
{
    m_timeline->setDuration(0);
    QCOMPARE(m_timeline->duration(), qint64(0));
}

void TestVideoTimeline::testSetDuration_LargeDuration()
{
    qint64 oneHour = 60 * 60 * 1000;
    m_timeline->setDuration(oneHour);
    QCOMPARE(m_timeline->duration(), oneHour);
}

void TestVideoTimeline::testDuration_DefaultValue()
{
    QCOMPARE(m_timeline->duration(), qint64(0));
}

// ============================================================================
// Position Tests
// ============================================================================

void TestVideoTimeline::testSetPosition_UpdatesValue()
{
    m_timeline->setDuration(10000);
    m_timeline->setPosition(5000);
    QCOMPARE(m_timeline->position(), qint64(5000));
}

void TestVideoTimeline::testSetPosition_ClampsToDuration()
{
    m_timeline->setDuration(10000);
    m_timeline->setPosition(15000);  // Beyond duration
    QCOMPARE(m_timeline->position(), qint64(10000));
}

void TestVideoTimeline::testSetPosition_ClampsToZero()
{
    m_timeline->setDuration(10000);
    m_timeline->setPosition(-1000);  // Negative
    QCOMPARE(m_timeline->position(), qint64(0));
}

void TestVideoTimeline::testSetPosition_IgnoredWhileScrubbing()
{
    m_timeline->setDuration(10000);
    m_timeline->setPosition(5000);

    // Simulate scrubbing start
    QTest::mousePress(m_timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 12));
    QVERIFY(m_timeline->isScrubbing());

    // External position update should be ignored
    qint64 posBefore = m_timeline->position();
    m_timeline->setPosition(1000);
    QCOMPARE(m_timeline->position(), posBefore);

    QTest::mouseRelease(m_timeline, Qt::LeftButton);
}

void TestVideoTimeline::testPosition_DefaultValue()
{
    QCOMPARE(m_timeline->position(), qint64(0));
}

// ============================================================================
// Position/X Conversion Tests
// ============================================================================

void TestVideoTimeline::testPositionFromX_AtStart()
{
    m_timeline->setDuration(10000);
    QRect track = testable()->trackRect();

    qint64 pos = testable()->positionFromX(track.left());
    QCOMPARE(pos, qint64(0));
}

void TestVideoTimeline::testPositionFromX_AtEnd()
{
    m_timeline->setDuration(10000);
    QRect track = testable()->trackRect();

    qint64 pos = testable()->positionFromX(track.right());
    QCOMPARE(pos, qint64(10000));
}

void TestVideoTimeline::testPositionFromX_Middle()
{
    m_timeline->setDuration(10000);
    QRect track = testable()->trackRect();

    int midX = (track.left() + track.right()) / 2;
    qint64 pos = testable()->positionFromX(midX);

    // Should be approximately 5000ms (middle)
    QVERIFY(pos >= 4500 && pos <= 5500);
}

void TestVideoTimeline::testPositionFromX_ZeroDuration()
{
    m_timeline->setDuration(0);
    qint64 pos = testable()->positionFromX(100);
    QCOMPARE(pos, qint64(0));
}

void TestVideoTimeline::testPositionFromX_ClampsToTrack()
{
    m_timeline->setDuration(10000);

    // X before track start
    qint64 pos1 = testable()->positionFromX(-100);
    QCOMPARE(pos1, qint64(0));

    // X after track end
    qint64 pos2 = testable()->positionFromX(10000);
    QCOMPARE(pos2, qint64(10000));
}

void TestVideoTimeline::testXFromPosition_AtStart()
{
    m_timeline->setDuration(10000);
    QRect track = testable()->trackRect();

    int x = testable()->xFromPosition(0);
    QCOMPARE(x, track.left());
}

void TestVideoTimeline::testXFromPosition_AtEnd()
{
    m_timeline->setDuration(10000);
    QRect track = testable()->trackRect();

    int x = testable()->xFromPosition(10000);
    QCOMPARE(x, track.left() + track.width());
}

void TestVideoTimeline::testXFromPosition_Middle()
{
    m_timeline->setDuration(10000);
    QRect track = testable()->trackRect();

    int x = testable()->xFromPosition(5000);
    int expectedX = track.left() + track.width() / 2;

    QVERIFY(qAbs(x - expectedX) <= 1);
}

void TestVideoTimeline::testXFromPosition_ZeroDuration()
{
    m_timeline->setDuration(0);
    int x = testable()->xFromPosition(0);
    // Should return horizontal padding (start position)
    QCOMPARE(x, 8);  // kHorizontalPadding
}

// ============================================================================
// Time Formatting Tests
// ============================================================================

void TestVideoTimeline::testFormatTime_Seconds()
{
    QString result = testable()->formatTime(5000);  // 5 seconds
    QCOMPARE(result, QString("5.0s"));
}

void TestVideoTimeline::testFormatTime_MinutesSeconds()
{
    QString result = testable()->formatTime(125000);  // 2:05
    QCOMPARE(result, QString("2:05.0"));
}

void TestVideoTimeline::testFormatTime_Zero()
{
    QString result = testable()->formatTime(0);
    QCOMPARE(result, QString("0.0s"));
}

void TestVideoTimeline::testFormatTime_Milliseconds()
{
    QString result = testable()->formatTime(5500);  // 5.5 seconds
    QCOMPARE(result, QString("5.5s"));

    QString result2 = testable()->formatTime(65500);  // 1:05.5
    QCOMPARE(result2, QString("1:05.5"));
}

// ============================================================================
// Scrubbing State Tests
// ============================================================================

void TestVideoTimeline::testIsScrubbing_DefaultFalse()
{
    QVERIFY(!m_timeline->isScrubbing());
}

void TestVideoTimeline::testScrubbing_StateChanges()
{
    m_timeline->setDuration(10000);

    QVERIFY(!m_timeline->isScrubbing());

    QTest::mousePress(m_timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 12));
    QVERIFY(m_timeline->isScrubbing());

    QTest::mouseRelease(m_timeline, Qt::LeftButton);
    QVERIFY(!m_timeline->isScrubbing());
}

// ============================================================================
// Size Hint Tests
// ============================================================================

void TestVideoTimeline::testSizeHint_ReturnsValid()
{
    QSize hint = testable()->sizeHint();
    QVERIFY(hint.width() > 0);
    QVERIFY(hint.height() > 0);
    QCOMPARE(hint.height(), 24);  // kHeight
}

void TestVideoTimeline::testMinimumSizeHint_ReturnsValid()
{
    QSize minHint = testable()->minimumSizeHint();
    QVERIFY(minHint.width() > 0);
    QVERIFY(minHint.height() > 0);
    QVERIFY(minHint.width() <= testable()->sizeHint().width());
}

// ============================================================================
// Signal Tests
// ============================================================================

void TestVideoTimeline::testSeekRequested_EmittedOnClick()
{
    m_timeline->setDuration(10000);

    QSignalSpy seekSpy(m_timeline, &VideoTimeline::seekRequested);

    QTest::mouseClick(m_timeline, Qt::LeftButton, Qt::NoModifier, QPoint(200, 12));

    QVERIFY(seekSpy.count() >= 1);
    qint64 seekPos = seekSpy.first().first().toLongLong();
    QVERIFY(seekPos >= 0 && seekPos <= 10000);
}

void TestVideoTimeline::testSeekRequested_EmittedOnDrag()
{
    m_timeline->setDuration(10000);

    QSignalSpy seekSpy(m_timeline, &VideoTimeline::seekRequested);

    QTest::mousePress(m_timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 12));
    QTest::mouseMove(m_timeline, QPoint(150, 12));
    QTest::mouseMove(m_timeline, QPoint(200, 12));
    QTest::mouseRelease(m_timeline, Qt::LeftButton);

    // Should have emitted multiple seek signals during drag
    QVERIFY(seekSpy.count() >= 2);
}

void TestVideoTimeline::testScrubbingStarted_EmittedOnPress()
{
    m_timeline->setDuration(10000);

    QSignalSpy scrubStartSpy(m_timeline, &VideoTimeline::scrubbingStarted);

    QTest::mousePress(m_timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 12));

    QCOMPARE(scrubStartSpy.count(), 1);

    QTest::mouseRelease(m_timeline, Qt::LeftButton);
}

void TestVideoTimeline::testScrubbingEnded_EmittedOnRelease()
{
    m_timeline->setDuration(10000);

    QSignalSpy scrubEndSpy(m_timeline, &VideoTimeline::scrubbingEnded);

    QTest::mousePress(m_timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 12));
    QCOMPARE(scrubEndSpy.count(), 0);

    QTest::mouseRelease(m_timeline, Qt::LeftButton);
    QCOMPARE(scrubEndSpy.count(), 1);
}

// ============================================================================
// Edge Cases
// ============================================================================

void TestVideoTimeline::testEdgeCase_SetPositionBeforeDuration()
{
    // Set position before duration is set
    m_timeline->setPosition(5000);
    QCOMPARE(m_timeline->position(), qint64(0));  // Clamped to 0 (duration is 0)

    // Now set duration
    m_timeline->setDuration(10000);
    m_timeline->setPosition(5000);
    QCOMPARE(m_timeline->position(), qint64(5000));
}

void TestVideoTimeline::testEdgeCase_RapidPositionChanges()
{
    m_timeline->setDuration(10000);

    // Rapidly change position
    for (int i = 0; i <= 100; ++i) {
        m_timeline->setPosition(i * 100);
    }

    QCOMPARE(m_timeline->position(), qint64(10000));
}

QTEST_MAIN(TestVideoTimeline)
#include "tst_VideoTimeline.moc"
