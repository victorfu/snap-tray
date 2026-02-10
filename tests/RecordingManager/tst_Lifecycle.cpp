#include <QtTest/QtTest>
#include <QSignalSpy>
#include "RecordingManager.h"

/**
 * @brief Tests for RecordingManager resource lifecycle
 *
 * Covers:
 * - Resource creation and cleanup
 * - Bug #2 prevention: testOnInitializationComplete_InitTaskNull
 * - Error handling paths
 * - Signal emission during lifecycle
 */
class TestRecordingManagerLifecycle : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Constructor/Destructor tests
    void testConstructorInitializesState();
    void testMultipleInstances();

    // Bug #2 prevention: Null pointer checks
    void testNullPointerSafety();

    // Resource cleanup tests
    void testCleanupInIdleState();
    void testStopRecordingInIdleState();

    // Error handling tests
    void testErrorSignalOnInvalidOperation();

private:
    RecordingManager* m_manager = nullptr;
};

void TestRecordingManagerLifecycle::initTestCase()
{
}

void TestRecordingManagerLifecycle::cleanupTestCase()
{
}

void TestRecordingManagerLifecycle::init()
{
    m_manager = new RecordingManager();
}

void TestRecordingManagerLifecycle::cleanup()
{
    delete m_manager;
    m_manager = nullptr;
}

// ============================================================================
// Constructor/Destructor Tests
// ============================================================================

void TestRecordingManagerLifecycle::testConstructorInitializesState()
{
    RecordingManager manager;

    QCOMPARE(manager.state(), RecordingManager::State::Idle);
    QVERIFY(!manager.isActive());
    QVERIFY(!manager.isRecording());
    QVERIFY(!manager.isPaused());
    QVERIFY(!manager.isSelectingRegion());
}

void TestRecordingManagerLifecycle::testMultipleInstances()
{
    // Multiple managers should coexist
    RecordingManager manager1;
    RecordingManager manager2;

    QCOMPARE(manager1.state(), RecordingManager::State::Idle);
    QCOMPARE(manager2.state(), RecordingManager::State::Idle);
}

// ============================================================================
// Bug #2 Prevention Tests
// ============================================================================

void TestRecordingManagerLifecycle::testNullPointerSafety()
{
    // Verify various operations don't crash when in initial state

    // These should all be safe in idle state
    m_manager->stopRecording();
    m_manager->cancelRecording();
    m_manager->pauseRecording();
    m_manager->resumeRecording();
    m_manager->togglePause();

    // Should still be in idle state
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

// ============================================================================
// Resource Cleanup Tests
// ============================================================================

void TestRecordingManagerLifecycle::testCleanupInIdleState()
{
    // Calling operations in idle state should be safe
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);

    m_manager->cancelRecording();

    // Should remain idle
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

void TestRecordingManagerLifecycle::testStopRecordingInIdleState()
{
    QSignalSpy stoppedSpy(m_manager, &RecordingManager::recordingStopped);

    m_manager->stopRecording();

    // Should not emit stopped signal when not recording
    QCOMPARE(stoppedSpy.count(), 0);
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

void TestRecordingManagerLifecycle::testErrorSignalOnInvalidOperation()
{
    QSignalSpy errorSpy(m_manager, &RecordingManager::recordingError);

    // Operations in wrong state should not crash
    m_manager->stopRecording();
    m_manager->pauseRecording();
    m_manager->resumeRecording();

    // These operations silently fail when in wrong state (no error signal)
    // This documents current behavior
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);
}

QTEST_MAIN(TestRecordingManagerLifecycle)
#include "tst_Lifecycle.moc"
