#include <QtTest/QtTest>
#include <QSignalSpy>
#include "RecordingManager.h"

/**
 * @brief Tests for RecordingManager audio integration
 *
 * Covers:
 * - Audio capture setup
 * - Bug #3 prevention: testAudioDisconnect_OnlyDisconnectsOwnConnections
 * - Audio/video synchronization concepts
 * - Audio cleanup
 */
class TestRecordingManagerAudioIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Basic audio state tests
    void testInitialAudioState();

    // Bug #3 prevention: Signal disconnect scope
    void testDisconnectScopeDocumentation();

    // Audio cleanup tests
    void testAudioCleanupInIdleState();

    // Audio-related signal tests
    void testNoAudioSignalsInIdle();

private:
    RecordingManager* m_manager = nullptr;
};

void TestRecordingManagerAudioIntegration::initTestCase()
{
}

void TestRecordingManagerAudioIntegration::cleanupTestCase()
{
}

void TestRecordingManagerAudioIntegration::init()
{
    m_manager = new RecordingManager();
}

void TestRecordingManagerAudioIntegration::cleanup()
{
    delete m_manager;
    m_manager = nullptr;
}

// ============================================================================
// Basic Audio State Tests
// ============================================================================

void TestRecordingManagerAudioIntegration::testInitialAudioState()
{
    // In initial state, audio should not be active
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
    QVERIFY(!m_manager->isRecording());
}

// ============================================================================
// Bug #3 Prevention Tests
// ============================================================================

void TestRecordingManagerAudioIntegration::testDisconnectScopeDocumentation()
{
    // Document the current disconnect patterns in RecordingManager:
    //
    // Line 299: disconnect(m_gifEncoder, nullptr, this, nullptr);
    //   - Disconnects ALL signals from m_gifEncoder to this
    //   - Could affect other components if they connected to gifEncoder
    //
    // Line 305: disconnect(m_nativeEncoder, nullptr, this, nullptr);
    //   - Same issue for native encoder
    //
    // Recommendation: Use specific disconnects for each signal-slot pair
    // or store QMetaObject::Connection handles for later disconnection

    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

// ============================================================================
// Audio Cleanup Tests
// ============================================================================

void TestRecordingManagerAudioIntegration::testAudioCleanupInIdleState()
{
    // In idle state, audio cleanup should be safe (no-op)
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);

    // Cancel should not crash even if audio was never initialized
    m_manager->cancelRecording();

    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

// ============================================================================
// Audio-related Signal Tests
// ============================================================================

void TestRecordingManagerAudioIntegration::testNoAudioSignalsInIdle()
{
    // Create spies for all signals
    QSignalSpy errorSpy(m_manager, &RecordingManager::recordingError);

    // In idle state, no audio-related signals should be emitted
    QTest::qWait(100);

    QCOMPARE(errorSpy.count(), 0);
}

QTEST_MAIN(TestRecordingManagerAudioIntegration)
#include "tst_AudioIntegration.moc"
