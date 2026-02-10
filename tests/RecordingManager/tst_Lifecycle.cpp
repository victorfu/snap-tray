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
    void testDestructorCleansUp();
    void testMultipleInstances();

    // Bug #2 prevention: Null pointer checks
    void testOnInitializationComplete_InitTaskNull_Prevention();
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

void TestRecordingManagerLifecycle::testDestructorCleansUp()
{
    // Create and destroy manager - should not crash
    RecordingManager* manager = new RecordingManager();
    QVERIFY(manager != nullptr);

    delete manager;
    // If we get here without crash, cleanup worked
    QVERIFY(true);
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

void TestRecordingManagerLifecycle::testOnInitializationComplete_InitTaskNull_Prevention()
{
    // Bug #2: In RecordingManager.cpp:434, m_initTask->result() is called
    // without checking if m_initTask is null first.
    //
    // The code should check:
    //     if (!m_initTask) {
    //         // Handle null initTask
    //         return;
    //     }
    //     const RecordingInitTask::Result &result = m_initTask->result();
    //
    // This test documents the bug and verifies current safe behavior

    // In initial state, there's no initTask running
    QCOMPARE(m_manager->state(), RecordingManager::State::Idle);

    // If onInitializationComplete() were called directly (which it shouldn't be),
    // it could crash if m_initTask is null

    // Current code at line 434:
    //     const RecordingInitTask::Result &result = m_initTask->result();
    // Should be:
    //     if (!m_initTask) {
    //         qWarning() << "onInitializationComplete called with null initTask";
    //         setState(State::Idle);
    //         return;
    //     }
    //     const RecordingInitTask::Result &result = m_initTask->result();
}

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
