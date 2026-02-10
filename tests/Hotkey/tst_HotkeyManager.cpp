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

    // Conflict detection tests
    void testHasConflict_NoConflict();
    void testHasConflict_DetectsConflict();
    void testHasConflict_ExcludesSelf();

    // Reset tests
    void testResetToDefault_RestoresOriginal();
    void testResetAllToDefaults_RestoresAll();

    // Format key sequence tests
    void testFormatKeySequence_StandardSequence();
    void testFormatKeySequence_EmptySequence();
    void testFormatKeySequence_NativeCode();

private:
    void clearAllTestSettings();
    void clearSetting(const char* key);

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
    manager().resetAllToDefaults();
    clearAllTestSettings();
}

// cleanupTestCase is called once after all tests complete
void tst_HotkeyManager::cleanupTestCase()
{
    // Shutdown manager to properly unregister all hotkeys before test exit
    manager().shutdown();
}

void tst_HotkeyManager::clearAllTestSettings()
{
    clearSetting(SnapTray::kSettingsKeyHotkey);
    clearSetting(SnapTray::kSettingsKeyScrollCaptureHotkey);
    clearSetting(SnapTray::kSettingsKeyScreenCanvasHotkey);
    clearSetting(SnapTray::kSettingsKeyPasteHotkey);
    clearSetting(SnapTray::kSettingsKeyQuickPinHotkey);
    clearSetting(SnapTray::kSettingsKeyPinFromImageHotkey);
    clearSetting(SnapTray::kSettingsKeyPinHistoryHotkey);
    clearSetting(SnapTray::kSettingsKeyRecordFullScreenHotkey);
}

void tst_HotkeyManager::clearSetting(const char* key)
{
    auto settings = SnapTray::getSettings();
    settings.remove(key);
    settings.remove(QString(key) + "_enabled");
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

// ============================================================================
// Config Retrieval Tests
// ============================================================================

void tst_HotkeyManager::testGetConfig_AllActionsExist()
{
    using namespace SnapTray;

    // Verify all expected actions have configs
    QVERIFY(!manager().getConfig(HotkeyAction::RegionCapture).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::ScrollCapture).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::ScreenCanvas).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::PasteFromClipboard).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::QuickPin).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::PinFromImage).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::PinHistory).displayName.isEmpty());
    QVERIFY(!manager().getConfig(HotkeyAction::RecordFullScreen).displayName.isEmpty());
}

void tst_HotkeyManager::testGetConfig_HasCorrectDefaults()
{
    using namespace SnapTray;

    auto regionCaptureConfig = manager().getConfig(HotkeyAction::RegionCapture);
    QCOMPARE(regionCaptureConfig.defaultKeySequence, QString(kDefaultHotkey));

    auto scrollCaptureConfig = manager().getConfig(HotkeyAction::ScrollCapture);
    QCOMPARE(scrollCaptureConfig.defaultKeySequence, QString(kDefaultScrollCaptureHotkey));

    auto screenCanvasConfig = manager().getConfig(HotkeyAction::ScreenCanvas);
    QCOMPARE(screenCanvasConfig.defaultKeySequence, QString(kDefaultScreenCanvasHotkey));
}

void tst_HotkeyManager::testGetConfig_CategoryAssignment()
{
    using namespace SnapTray;

    QCOMPARE(manager().getConfig(HotkeyAction::RegionCapture).category, HotkeyCategory::Capture);
    QCOMPARE(manager().getConfig(HotkeyAction::ScrollCapture).category, HotkeyCategory::Capture);
    QCOMPARE(manager().getConfig(HotkeyAction::ScreenCanvas).category, HotkeyCategory::Canvas);
    QCOMPARE(manager().getConfig(HotkeyAction::PasteFromClipboard).category, HotkeyCategory::Clipboard);
    QCOMPARE(manager().getConfig(HotkeyAction::QuickPin).category, HotkeyCategory::Pin);
    QCOMPARE(manager().getConfig(HotkeyAction::PinFromImage).category, HotkeyCategory::Pin);
    QCOMPARE(manager().getConfig(HotkeyAction::PinHistory).category, HotkeyCategory::Pin);
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
    QCOMPARE(captureConfigs.size(), 2);
    QVERIFY(captureConfigs[0].action == HotkeyAction::RegionCapture
            || captureConfigs[1].action == HotkeyAction::RegionCapture);
    QVERIFY(captureConfigs[0].action == HotkeyAction::ScrollCapture
            || captureConfigs[1].action == HotkeyAction::ScrollCapture);

    auto pinConfigs = manager().getConfigsByCategory(HotkeyCategory::Pin);
    QCOMPARE(pinConfigs.size(), 3);  // QuickPin, PinFromImage, PinHistory
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
    manager().updateHotkey(HotkeyAction::RegionCapture, "Ctrl+Shift+X");

    auto modified = manager().getConfig(HotkeyAction::RegionCapture);
    QCOMPARE(modified.keySequence, QString("Ctrl+Shift+X"));

    // Reset to default
    manager().resetToDefault(HotkeyAction::RegionCapture);

    auto restored = manager().getConfig(HotkeyAction::RegionCapture);
    QCOMPARE(restored.keySequence, restored.defaultKeySequence);
}

void tst_HotkeyManager::testResetAllToDefaults_RestoresAll()
{
    using namespace SnapTray;

    // Modify multiple hotkeys
    manager().updateHotkey(HotkeyAction::RegionCapture, "Ctrl+1");
    manager().updateHotkey(HotkeyAction::ScreenCanvas, "Ctrl+2");

    // Reset all
    manager().resetAllToDefaults();

    // Verify all are restored
    auto configs = manager().getAllConfigs();
    for (const auto& config : configs) {
        QCOMPARE(config.keySequence, config.defaultKeySequence);
    }
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
