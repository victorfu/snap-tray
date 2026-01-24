/**
 * @file HotkeyManager.h
 * @brief Singleton manager for all application hotkeys
 *
 * Centralizes hotkey registration, conflict detection, and settings persistence.
 * Emits a single actionTriggered signal when any hotkey is activated.
 */

#pragma once

#include "HotkeyTypes.h"
#include <QObject>
#include <QMap>
#include <optional>

class QHotkey;

namespace SnapTray {

/**
 * @brief Singleton manager for all application hotkeys.
 *
 * Usage:
 * @code
 * // Initialize at startup
 * HotkeyManager::instance().initialize();
 *
 * // Connect to receive hotkey activations
 * connect(&HotkeyManager::instance(), &HotkeyManager::actionTriggered,
 *         this, &MyClass::onHotkeyAction);
 *
 * // Update a hotkey
 * bool success = HotkeyManager::instance().updateHotkey(HotkeyAction::RegionCapture, "Ctrl+Shift+S");
 * @endcode
 */
class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Get the singleton instance.
     */
    static HotkeyManager& instance();

    /**
     * @brief Initialize the manager and load all hotkeys from settings.
     *
     * Must be called once at application startup.
     * Loads configurations from QSettings and registers enabled hotkeys.
     */
    void initialize();

    /**
     * @brief Shutdown the manager and unregister all hotkeys.
     *
     * Should be called at application exit.
     */
    void shutdown();

    /**
     * @brief Check if the manager has been initialized.
     */
    bool isInitialized() const { return m_initialized; }

    // ========================================================================
    // Configuration Access
    // ========================================================================

    /**
     * @brief Get the configuration for a specific action.
     */
    HotkeyConfig getConfig(HotkeyAction action) const;

    /**
     * @brief Get all hotkey configurations.
     */
    QList<HotkeyConfig> getAllConfigs() const;

    /**
     * @brief Get configurations filtered by category.
     */
    QList<HotkeyConfig> getConfigsByCategory(HotkeyCategory category) const;

    /**
     * @brief Get the display text for a key sequence (platform-native format).
     */
    static QString formatKeySequence(const QString& keySequence);

    // ========================================================================
    // Hotkey Modification
    // ========================================================================

    /**
     * @brief Update a hotkey's key sequence.
     *
     * @param action The action to update
     * @param keySequence New key sequence (empty string disables the hotkey)
     * @return true if registration succeeded or hotkey was cleared
     */
    bool updateHotkey(HotkeyAction action, const QString& keySequence);

    /**
     * @brief Enable or disable a hotkey without changing its sequence.
     *
     * @param action The action to enable/disable
     * @param enabled Whether to enable the hotkey
     * @return true if the operation succeeded
     */
    bool setHotkeyEnabled(HotkeyAction action, bool enabled);

    /**
     * @brief Reset a single hotkey to its default value.
     *
     * @param action The action to reset
     * @return true if the reset succeeded
     */
    bool resetToDefault(HotkeyAction action);

    /**
     * @brief Reset all hotkeys to their default values.
     */
    void resetAllToDefaults();

    /**
     * @brief Temporarily suspend all hotkey registrations.
     *
     * Use this when showing a key capture dialog to prevent
     * global hotkeys from triggering during key input.
     * Call resumeRegistration() when done.
     */
    void suspendRegistration();

    /**
     * @brief Resume hotkey registrations after suspension.
     *
     * Re-registers all enabled hotkeys that were active before suspension.
     */
    void resumeRegistration();

    // ========================================================================
    // Conflict Detection
    // ========================================================================

    /**
     * @brief Check if a key sequence conflicts with another registered hotkey.
     *
     * @param keySequence The sequence to check
     * @param excludeAction Optionally exclude an action from conflict check (for self-update)
     * @return The conflicting action, or std::nullopt if no conflict
     */
    std::optional<HotkeyAction> hasConflict(const QString& keySequence,
                                             std::optional<HotkeyAction> excludeAction = std::nullopt) const;

    /**
     * @brief Get the display name of the conflicting hotkey, if any.
     */
    QString getConflictDescription(const QString& keySequence,
                                    std::optional<HotkeyAction> excludeAction = std::nullopt) const;

signals:
    /**
     * @brief Emitted when any hotkey is activated.
     *
     * Connect to this signal to handle hotkey actions in a centralized way.
     */
    void actionTriggered(SnapTray::HotkeyAction action);

    /**
     * @brief Emitted when a hotkey's configuration changes.
     *
     * Use this to update UI elements displaying hotkey information.
     */
    void hotkeyChanged(SnapTray::HotkeyAction action, const SnapTray::HotkeyConfig& config);

    /**
     * @brief Emitted when hotkey registration status changes.
     */
    void registrationStatusChanged(SnapTray::HotkeyAction action, SnapTray::HotkeyStatus status);

private:
    HotkeyManager();
    ~HotkeyManager() override;

    // Non-copyable
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    // Internal helpers
    void initializeConfigs();
    void loadFromSettings();
    void saveToSettings(HotkeyAction action);

    bool registerHotkey(HotkeyAction action);
    void unregisterHotkey(HotkeyAction action);
    void unregisterAllHotkeys();

    void createHotkeyInstance(HotkeyAction action);
    void onHotkeyActivated(HotkeyAction action);

    bool isNativeKeyCode(const QString& keySequence, quint32& nativeKey) const;

    // Storage
    QMap<HotkeyAction, HotkeyConfig> m_configs;
    QMap<HotkeyAction, QHotkey*> m_hotkeys;
    bool m_initialized = false;
};

}  // namespace SnapTray

// Register metatypes for signal/slot usage
Q_DECLARE_METATYPE(SnapTray::HotkeyAction)
Q_DECLARE_METATYPE(SnapTray::HotkeyConfig)
Q_DECLARE_METATYPE(SnapTray::HotkeyStatus)
