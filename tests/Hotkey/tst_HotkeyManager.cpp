/**
 * @file tst_HotkeyManager.cpp
 * @brief Unit tests for HotkeyManager singleton class.
 *
 * Tests HotkeyManager functionality including:
 * - Singleton pattern and initialization
 * - Configuration retrieval by action
 * - Hotkey update and persistence
 * - Conflict detection between hotkeys
 * - Reset to defaults functionality
 * - Backward compatibility with existing settings
 */

#include <QtTest>
#include <QSettings>
#include <QSignalSpy>
#include "hotkey/HotkeyManager.h"
#include "hotkey/HotkeyTypes.h"
#include "settings/Settings.h"

class tst_HotkeyManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void cleanupTestCase();

    // Singleton tests
    void testSingletonInstance();
    void testInitialize_EmitsInitializationCompleted();
    void testInitialize_PersistedConflictKeepsPreferredOwnerActive();

    // Config retrieval tests
    void testGetConfig_AllActionsExist();
    void testGetConfig_HasCorrectDefaults();
    void testGetConfig_CategoryAssignment();

    // Config list tests
    void testGetAllConfigs_ReturnsAllHotkeys();
    void testGetConfigsByCategory_FilterCorrectly();

    // Update tests
    void testUpdateHotkey_EmitsSignals();
    void testUpdateHotkey_ClearsHotkey();
    void testUpdateHotkey_DisabledRollbackStatus();
    void testUpdateHotkey_ConflictPersistsDesiredSequence();
    void testUpdateHotkey_ReleasesPersistedConflictForOtherAction();

    // Conflict detection tests
    void testHasConflict_NoConflict();
    void testHasConflict_DetectsConflict();
    void testHasConflict_ExcludesSelf();

    // Reset tests
    void testResetToDefault_RestoresOriginal();
    void testResetAllToDefaults_RestoresAll();
    void testResetAllToDefaults_ReregistersRecoveredDefaultHotkey();

    // Format key sequence tests
    void testFormatKeySequence_StandardSequence();
    void testFormatKeySequence_EmptySequence();
    void testFormatKeySequence_NativeCode();

private:
    void clearAllTestSettings();
    void clearSetting(const char* key);
    void setRegistrationOrder(const char* key, quint64 order);
    QString registrationOrderKey(const char* key) const;

    SnapTray::HotkeyManager& manager() { return SnapTray::HotkeyManager::instance(); }
};

void tst_HotkeyManager::init()
{
    clearAllTestSettings();
    // Ensure manager is initialized for tests
    // Note: initialize() is idempotent - safe to call multiple times
    manager().initialize();
}

void tst_HotkeyManager::cleanup()
{
    // Reset all hotkeys to defaults to ensure clean state
    manager().m_registerHotkeyOverride = {};
    manager().resetAllToDefaults();
    clearAllTestSettings();
}

// cleanupTestCase is called once after all tests complete
void tst_HotkeyManager::cleanupTestCase()
{
    // Shutdown manager to properly unregister all hotkeys before test exit
    manager().m_registerHotkeyOverride = {};
    manager().shutdown();
}

void tst_HotkeyManager::clearAllTestSettings()
{
    clearSetting(SnapTray::kSettingsKeyHotkey);
    clearSetting(SnapTray::kSettingsKeyScreenCanvasHotkey);
    clearSetting(SnapTray::kSettingsKeyPasteHotkey);
    clearSetting(SnapTray::kSettingsKeyQuickPinHotkey);
    clearSetting(SnapTray::kSettingsKeyPinFromImageHotkey);
    clearSetting(SnapTray::kSettingsKeyHistoryWindowHotkey);
    clearSetting(SnapTray::kSettingsKeyTogglePinsVisibilityHotkey);
    clearSetting(SnapTray::kSettingsKeyRecordFullScreenHotkey);

    auto settings = SnapTray::getSettings();
    settings.remove(QString::fromLatin1(SnapTray::kSettingsKeyHotkeyRegistrationCounter));
}

void tst_HotkeyManager::clearSetting(const char* key)
{
    auto settings = SnapTray::getSettings();
    settings.remove(key);
    settings.remove(QString::fromUtf8(key) + QString::fromLatin1(SnapTray::kSettingsKeyHotkeyEnabledSuffix));
    settings.remove(registrationOrderKey(key));
}

void tst_HotkeyManager::setRegistrationOrder(const char* key, quint64 order)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(registrationOrderKey(key), order);
}

QString tst_HotkeyManager::registrationOrderKey(const char* key) const
{
    return QString::fromUtf8(key)
        + QString::fromLatin1(SnapTray::kSettingsKeyHotkeyRegistrationOrderSuffix);
}

// ============================================================================
// Singleton Tests
// ============================================================================

void tst_HotkeyManager::testSingletonInstance()
{
    auto& instance1 = SnapTray::HotkeyManager::instance();
    auto& instance2 = SnapTray::HotkeyManager::instance();

    QCOMPARE(&instance1, &instance2);
}

void tst_HotkeyManager::testInitialize_EmitsInitializationCompleted()
{
    manager().shutdown();

    QSignalSpy initializeSpy(&manager(), &SnapTray::HotkeyManager::initializationCompleted);
    QVERIFY(initializeSpy.isValid());

    manager().initialize();
    QCOMPARE(initializeSpy.count(), 1);

    const QList<QVariant> args = initializeSpy.takeFirst();
    QCOMPARE(args.size(), 1);
    QVERIFY(args.at(0).canConvert<QStringList>());

    // initialize() is idempotent and should not emit again when already initialized.
    manager().initialize();
    QCOMPARE(initializeSpy.count(), 0);
}

void tst_HotkeyManager::testInitialize_PersistedConflictKeepsPreferredOwnerActive()
{
    using namespace SnapTray;

    manager().shutdown();
    clearAllTestSettings();

    auto settings = getSettings();
    settings.setValue(kSettingsKeyHotkey, "Ctrl+Alt+1");
    settings.setValue(kSettingsKeyPinFromImageHotkey, "Ctrl+Alt+1");
    setRegistrationOrder(kSettingsKeyHotkey, 7);
    settings.setValue(QString::fromLatin1(kSettingsKeyHotkeyRegistrationCounter), 8);

    manager().initialize();

    QVERIFY(manager().m_hotkeys.contains(HotkeyAction::RegionCapture));
    QVERIFY(!manager().m_hotkeys.contains(HotkeyAction::PinFromImage));
    QCOMPARE(manager().getConfig(HotkeyAction::PinFromImage).status, HotkeyStatus::Failed);
}

// ============================================================================
// Config Retrieval Tests
// ============================================================================

void tst_HotkeyManager::testGetConfig_AllActionsExist()
{
    using namespace SnapTray;

    // Verify all expected actions have configs
    QVERIFY(!manager().getConfig(HotkeyAction::RegionCapture).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::ScreenCanvas).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::PasteFromClipboard).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::QuickPin).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::PinFromImage).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::HistoryWindow).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::TogglePinsVisibility).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::RecordFullScreen).displayName.isEmpty());
}

void tst_HotkeyManager::testGetConfig_HasCorrectDefaults()
{
    using namespace SnapTray;

    auto regionCaptureConfig = manager().getConfig(HotkeyAction::RegionCapture);
    QCOMPARE(regionCaptureConfig.defaultKeySequence, QString(kDefaultHotkey));

    auto screenCanvasConfig = manager().getConfig(HotkeyAction::ScreenCanvas);
    QCOMPARE(screenCanvasConfig.defaultKeySequence, QString(kDefaultScreenCanvasHotkey));
}

void tst_HotkeyManager::testGetConfig_CategoryAssignment()
{
    using namespace SnapTray;

    QCOMPARE(manager().getConfig(HotkeyAction::RegionCapture).category, HotkeyCategory::Capture);
    QCOMPARE(manager().getConfig(HotkeyAction::ScreenCanvas).category, HotkeyCategory::Canvas);
    QCOMPARE(manager().getConfig(HotkeyAction::PasteFromClipboard).category, HotkeyCategory::Clipboard);
    QCOMPARE(manager().getConfig(HotkeyAction::QuickPin).category, HotkeyCategory::Pin);
    QCOMPARE(manager().getConfig(HotkeyAction::PinFromImage).category, HotkeyCategory::Pin);
    QCOMPARE(manager().getConfig(HotkeyAction::HistoryWindow).category, HotkeyCategory::Pin);
    QCOMPARE(manager().getConfig(HotkeyAction::TogglePinsVisibility).category, HotkeyCategory::Pin);
    QCOMPARE(manager().getConfig(HotkeyAction::RecordFullScreen).category, HotkeyCategory::Recording);
}

// ============================================================================
// Config List Tests
// ============================================================================

void tst_HotkeyManager::testGetAllConfigs_ReturnsAllHotkeys()
{
    auto configs = manager().getAllConfigs();
    QCOMPARE(configs.size(), static_cast<int>(SnapTray::kDefaultHotkeyCount));
}

void tst_HotkeyManager::testGetConfigsByCategory_FilterCorrectly()
{
    using namespace SnapTray;

    auto captureConfigs = manager().getConfigsByCategory(HotkeyCategory::Capture);
    QCOMPARE(captureConfigs.size(), 1);
    QCOMPARE(captureConfigs.first().action, HotkeyAction::RegionCapture);

    auto pinConfigs = manager().getConfigsByCategory(HotkeyCategory::Pin);
    QCOMPARE(pinConfigs.size(), 4);  // QuickPin, PinFromImage, HistoryWindow, TogglePinsVisibility
}

// ============================================================================
// Update Tests
// ============================================================================

void tst_HotkeyManager::testUpdateHotkey_EmitsSignals()
{
    using namespace SnapTray;

    QSignalSpy changedSpy(&manager(), &HotkeyManager::hotkeyChanged);
    QSignalSpy statusSpy(&manager(), &HotkeyManager::registrationStatusChanged);

    QVERIFY(changedSpy.isValid());
    QVERIFY(statusSpy.isValid());

    // Update to a test sequence
    manager().updateHotkey(HotkeyAction::RegionCapture, "Ctrl+Shift+T");

    QVERIFY(changedSpy.count() >= 1);
    QVERIFY(statusSpy.count() >= 1);

    // Restore original
    manager().resetToDefault(HotkeyAction::RegionCapture);
}

void tst_HotkeyManager::testUpdateHotkey_ClearsHotkey()
{
    using namespace SnapTray;

    // Clear the hotkey
    bool result = manager().updateHotkey(HotkeyAction::PinFromImage, "");

    QVERIFY(result);
    auto config = manager().getConfig(HotkeyAction::PinFromImage);
    QVERIFY(config.keySequence.isEmpty());
    QCOMPARE(config.status, HotkeyStatus::Unset);

    // Restore
    manager().resetToDefault(HotkeyAction::PinFromImage);
}

void tst_HotkeyManager::testUpdateHotkey_DisabledRollbackStatus()
{
    using namespace SnapTray;

    constexpr HotkeyAction action = HotkeyAction::RegionCapture;

    QVERIFY(manager().setHotkeyEnabled(action, false));

    const HotkeyConfig disabledConfig = manager().getConfig(action);
    QCOMPARE(disabledConfig.status, HotkeyStatus::Disabled);
    QVERIFY(!disabledConfig.keySequence.isEmpty());

    QSignalSpy changedSpy(&manager(), &HotkeyManager::hotkeyChanged);
    QSignalSpy statusSpy(&manager(), &HotkeyManager::registrationStatusChanged);
    QVERIFY(changedSpy.isValid());
    QVERIFY(statusSpy.isValid());

    const bool result = manager().updateHotkey(action, "Ctrl+Shift+T");

    QVERIFY(result);

    const HotkeyConfig config = manager().getConfig(action);
    QCOMPARE(config.keySequence, QString("Ctrl+Shift+T"));
    QCOMPARE(config.status, HotkeyStatus::Disabled);
    QCOMPARE(config.enabled, false);

    QVERIFY(changedSpy.count() >= 1);
    QVERIFY(statusSpy.count() >= 1);

    const QList<QVariant> changedArgs = changedSpy.last();
    QCOMPARE(changedArgs.at(0).value<HotkeyAction>(), action);
    const HotkeyConfig emittedConfig = changedArgs.at(1).value<HotkeyConfig>();
    QCOMPARE(emittedConfig.keySequence, QString("Ctrl+Shift+T"));
    QCOMPARE(emittedConfig.status, HotkeyStatus::Disabled);

    const QList<QVariant> statusArgs = statusSpy.last();
    QCOMPARE(statusArgs.at(0).value<HotkeyAction>(), action);
    QCOMPARE(statusArgs.at(1).value<HotkeyStatus>(), HotkeyStatus::Disabled);
}

void tst_HotkeyManager::testUpdateHotkey_ConflictPersistsDesiredSequence()
{
    using namespace SnapTray;

    const QString blockedSequence = manager().getConfig(HotkeyAction::ScreenCanvas).keySequence;
    QVERIFY(!blockedSequence.isEmpty());

    const bool result = manager().updateHotkey(HotkeyAction::PinFromImage, blockedSequence);

    QVERIFY(!result);

    const HotkeyConfig config = manager().getConfig(HotkeyAction::PinFromImage);
    QCOMPARE(config.keySequence, blockedSequence);
    QCOMPARE(config.status, HotkeyStatus::Failed);
    QVERIFY(config.enabled);
}

void tst_HotkeyManager::testUpdateHotkey_ReleasesPersistedConflictForOtherAction()
{
    using namespace SnapTray;

    manager().shutdown();
    clearAllTestSettings();
    manager().m_registerHotkeyOverride = [](HotkeyAction, const QString&) {
        return true;
    };
    manager().initialize();

    constexpr HotkeyAction owner = HotkeyAction::PinFromImage;
    constexpr HotkeyAction blocked = HotkeyAction::HistoryWindow;
    const QString sharedSequence = QStringLiteral("Ctrl+Alt+1");

    QVERIFY(manager().updateHotkey(owner, sharedSequence));
    QVERIFY(!manager().updateHotkey(blocked, sharedSequence));
    QCOMPARE(manager().getConfig(blocked).status, HotkeyStatus::Failed);

    QVERIFY(manager().updateHotkey(owner, QStringLiteral("Ctrl+Alt+2")));

    const HotkeyConfig recovered = manager().getConfig(blocked);
    QCOMPARE(recovered.keySequence, sharedSequence);
    QCOMPARE(recovered.status, HotkeyStatus::Registered);
    QVERIFY(manager().m_registrationOrders.value(blocked, 0) > 0);
}

// ============================================================================
// Conflict Detection Tests
// ============================================================================

void tst_HotkeyManager::testHasConflict_NoConflict()
{
    using namespace SnapTray;

    // A unique key sequence should not conflict
    auto conflict = manager().hasConflict("Ctrl+Alt+Shift+Z", std::nullopt);
    QVERIFY(!conflict.has_value());
}

void tst_HotkeyManager::testHasConflict_DetectsConflict()
{
    using namespace SnapTray;

    // Get the current Region Capture hotkey
    auto config = manager().getConfig(HotkeyAction::RegionCapture);

    // Check if that key sequence conflicts (should conflict with Region Capture)
    auto conflict = manager().hasConflict(config.keySequence, std::nullopt);

    // Should detect conflict with RegionCapture
    QVERIFY(conflict.has_value());
    QCOMPARE(*conflict, HotkeyAction::RegionCapture);
}

void tst_HotkeyManager::testHasConflict_ExcludesSelf()
{
    using namespace SnapTray;

    auto config = manager().getConfig(HotkeyAction::RegionCapture);

    // When excluding self, should not conflict
    auto conflict = manager().hasConflict(config.keySequence, HotkeyAction::RegionCapture);
    QVERIFY(!conflict.has_value());
}

// ============================================================================
// Reset Tests
// ============================================================================

void tst_HotkeyManager::testResetToDefault_RestoresOriginal()
{
    using namespace SnapTray;

    // Change the hotkey
    const bool changed = manager().updateHotkey(HotkeyAction::RegionCapture, "Ctrl+Shift+X");

    auto modified = manager().getConfig(HotkeyAction::RegionCapture);
    QCOMPARE(modified.keySequence, QString("Ctrl+Shift+X"));
    QCOMPARE(modified.status, changed ? HotkeyStatus::Registered : HotkeyStatus::Failed);

    // Reset to default
    const bool reset = manager().resetToDefault(HotkeyAction::RegionCapture);

    auto restored = manager().getConfig(HotkeyAction::RegionCapture);
    QCOMPARE(restored.keySequence, restored.defaultKeySequence);
    QCOMPARE(restored.enabled, true);
    QCOMPARE(restored.status, reset ? HotkeyStatus::Registered : HotkeyStatus::Failed);
}

void tst_HotkeyManager::testResetAllToDefaults_RestoresAll()
{
    using namespace SnapTray;

    // Modify multiple hotkeys
    manager().updateHotkey(HotkeyAction::RegionCapture, "Ctrl+1");
    manager().updateHotkey(HotkeyAction::ScreenCanvas, "Ctrl+2");

    // Reset all
    const QStringList failedHotkeys = manager().resetAllToDefaults();

    // Verify all are restored
    auto configs = manager().getAllConfigs();
    for (const auto& config : configs) {
        QCOMPARE(config.keySequence, config.defaultKeySequence);
        if (failedHotkeys.contains(config.displayName)) {
            QCOMPARE(config.status, HotkeyStatus::Failed);
        }
    }
}

void tst_HotkeyManager::testResetAllToDefaults_ReregistersRecoveredDefaultHotkey()
{
    using namespace SnapTray;

    manager().shutdown();
    clearAllTestSettings();

    auto settings = getSettings();
    settings.setValue(kSettingsKeyHotkey, QString(kDefaultHotkey));
    settings.setValue(kSettingsKeyPinFromImageHotkey, QString(kDefaultHotkey));

    manager().initialize();
    QVERIFY(manager().m_hotkeys.contains(HotkeyAction::RegionCapture));

    manager().resetAllToDefaults();

    QVERIFY(manager().m_hotkeys.contains(HotkeyAction::RegionCapture));
    QVERIFY(!manager().m_hotkeys.contains(HotkeyAction::PinFromImage));
    QVERIFY(manager().getConfig(HotkeyAction::PinFromImage).keySequence.isEmpty());
}

// ============================================================================
// Format Key Sequence Tests
// ============================================================================

void tst_HotkeyManager::testFormatKeySequence_StandardSequence()
{
    QString formatted = manager().formatKeySequence("Ctrl+Shift+A");
    QVERIFY(!formatted.isEmpty());
}

void tst_HotkeyManager::testFormatKeySequence_EmptySequence()
{
    QString formatted = manager().formatKeySequence("");
    QVERIFY(formatted.isEmpty());
}

void tst_HotkeyManager::testFormatKeySequence_NativeCode()
{
    // Native keycodes should be preserved as-is
    QString formatted = manager().formatKeySequence("Native:0x2C");
    QCOMPARE(formatted, QString("Native:0x2C"));
}

QTEST_MAIN(tst_HotkeyManager)
#include "tst_HotkeyManager.moc"
