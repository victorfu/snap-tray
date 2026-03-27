#include <QtTest>
#include <QSettings>
#include <QDateTime>
#include "update/UpdateSettingsManager.h"
#include "settings/Settings.h"

/**
 * @brief Unit tests for UpdateSettingsManager singleton class.
 *
 * Tests settings persistence including:
 * - Auto-check enabled/disabled
 * - Check interval hours
 * - Last check time
 */
class tst_UpdateSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Singleton tests
    void testSingletonInstance();

    // Auto-check settings tests
    void testIsAutoCheckEnabled_DefaultValue();
    void testSetAutoCheckEnabled_Roundtrip();

    // Check interval tests
    void testCheckIntervalHours_DefaultValue();
    void testSetCheckIntervalHours_Roundtrip();
    void testSetCheckIntervalHours_Clamping();

    // Last check time tests
    void testLastCheckTime_DefaultInvalid();
    void testSetLastCheckTime_Roundtrip();

private:
    void clearAllTestSettings();
};

void tst_UpdateSettingsManager::init()
{
    clearAllTestSettings();
}

void tst_UpdateSettingsManager::cleanup()
{
    clearAllTestSettings();
}

void tst_UpdateSettingsManager::clearAllTestSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("update/autoCheck");
    settings.remove("update/checkIntervalHours");
    settings.remove("update/lastCheckTime");
    settings.sync();
}

// ============================================================================
// Singleton tests
// ============================================================================

void tst_UpdateSettingsManager::testSingletonInstance()
{
    UpdateSettingsManager& instance1 = UpdateSettingsManager::instance();
    UpdateSettingsManager& instance2 = UpdateSettingsManager::instance();

    QCOMPARE(&instance1, &instance2);
}

// ============================================================================
// Auto-check settings tests
// ============================================================================

void tst_UpdateSettingsManager::testIsAutoCheckEnabled_DefaultValue()
{
    bool enabled = UpdateSettingsManager::instance().isAutoCheckEnabled();
    QCOMPARE(enabled, UpdateSettingsManager::kDefaultAutoCheck);
    QCOMPARE(enabled, true);
}

void tst_UpdateSettingsManager::testSetAutoCheckEnabled_Roundtrip()
{
    UpdateSettingsManager& manager = UpdateSettingsManager::instance();

    manager.setAutoCheckEnabled(false);
    QCOMPARE(manager.isAutoCheckEnabled(), false);

    manager.setAutoCheckEnabled(true);
    QCOMPARE(manager.isAutoCheckEnabled(), true);
}

// ============================================================================
// Check interval tests
// ============================================================================

void tst_UpdateSettingsManager::testCheckIntervalHours_DefaultValue()
{
    int hours = UpdateSettingsManager::instance().checkIntervalHours();
    QCOMPARE(hours, UpdateSettingsManager::kDefaultCheckIntervalHours);
    QCOMPARE(hours, 24);
}

void tst_UpdateSettingsManager::testSetCheckIntervalHours_Roundtrip()
{
    UpdateSettingsManager& manager = UpdateSettingsManager::instance();

    manager.setCheckIntervalHours(72);
    QCOMPARE(manager.checkIntervalHours(), 72);

    manager.setCheckIntervalHours(168);
    QCOMPARE(manager.checkIntervalHours(), 168);
}

void tst_UpdateSettingsManager::testSetCheckIntervalHours_Clamping()
{
    UpdateSettingsManager& manager = UpdateSettingsManager::instance();

    // Test minimum clamping
    manager.setCheckIntervalHours(0);
    QCOMPARE(manager.checkIntervalHours(), 1);

    manager.setCheckIntervalHours(-10);
    QCOMPARE(manager.checkIntervalHours(), 1);

    // Test maximum clamping
    manager.setCheckIntervalHours(1000);
    QCOMPARE(manager.checkIntervalHours(), 720);  // 1 month max
}

// ============================================================================
// Last check time tests
// ============================================================================

void tst_UpdateSettingsManager::testLastCheckTime_DefaultInvalid()
{
    QDateTime time = UpdateSettingsManager::instance().lastCheckTime();
    QVERIFY(!time.isValid());
}

void tst_UpdateSettingsManager::testSetLastCheckTime_Roundtrip()
{
    UpdateSettingsManager& manager = UpdateSettingsManager::instance();

    QDateTime testTime = QDateTime::currentDateTime();
    manager.setLastCheckTime(testTime);

    QDateTime loaded = manager.lastCheckTime();
    QVERIFY(loaded.isValid());
    // Allow 1 second tolerance for serialization
    QVERIFY(qAbs(loaded.secsTo(testTime)) <= 1);
}

QTEST_MAIN(tst_UpdateSettingsManager)
#include "tst_UpdateSettingsManager.moc"
