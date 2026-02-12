/**
 * @file HotkeyManager.cpp
 * @brief HotkeyManager implementation
 */

#include "hotkey/HotkeyManager.h"
#include "settings/Settings.h"
#include "ui/GlobalToast.h"

#include <QHotkey>
#include <QKeySequence>
#include <QSettings>

namespace SnapTray {

HotkeyManager::HotkeyManager()
    : QObject(nullptr)
{
    qRegisterMetaType<HotkeyAction>("HotkeyAction");
    qRegisterMetaType<HotkeyAction>("SnapTray::HotkeyAction");
    qRegisterMetaType<HotkeyConfig>("HotkeyConfig");
    qRegisterMetaType<HotkeyConfig>("SnapTray::HotkeyConfig");
    qRegisterMetaType<HotkeyStatus>("HotkeyStatus");
    qRegisterMetaType<HotkeyStatus>("SnapTray::HotkeyStatus");
}

HotkeyManager::~HotkeyManager()
{
    shutdown();
}

HotkeyManager& HotkeyManager::instance()
{
    static HotkeyManager s_instance;
    return s_instance;
}

void HotkeyManager::initialize()
{
    if (m_initialized) {
        return;
    }

    initializeConfigs();
    loadFromSettings();

    // Register all enabled hotkeys and update their status
    QStringList failedHotkeys;
    for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
        HotkeyConfig& config = it.value();

        if (config.keySequence.isEmpty()) {
            config.status = HotkeyStatus::Unset;
        } else if (!config.enabled) {
            config.status = HotkeyStatus::Disabled;
        } else if (registerHotkey(it.key())) {
            config.status = HotkeyStatus::Registered;
        } else {
            config.status = HotkeyStatus::Failed;
            failedHotkeys << config.displayName;
        }
    }

    // Notify about failures
    if (!failedHotkeys.isEmpty()) {
        GlobalToast::instance().showToast(
            GlobalToast::Error,
            tr("Hotkey Registration Failed"),
            failedHotkeys.join(QStringLiteral(", ")) + tr(" failed to register."),
            5000);
    }

    m_initialized = true;
}

void HotkeyManager::shutdown()
{
    if (!m_initialized) {
        return;
    }

    unregisterAllHotkeys();
    m_initialized = false;
}

void HotkeyManager::initializeConfigs()
{
    m_configs.clear();

    for (size_t i = 0; i < kDefaultHotkeyCount; ++i) {
        const auto& meta = kDefaultHotkeys[i];

        HotkeyConfig config;
        config.action = meta.action;
        config.category = meta.category;
        config.displayName = QString::fromUtf8(meta.displayName);
        config.description = QString::fromUtf8(meta.description);
        config.settingsKey = QString::fromUtf8(meta.settingsKey);
        config.defaultKeySequence = QString::fromUtf8(meta.defaultKeySequence);
        config.keySequence = config.defaultKeySequence;
        config.enabled = true;
        config.status = HotkeyStatus::Unset;

        m_configs[meta.action] = config;
    }
}

void HotkeyManager::loadFromSettings()
{
    QSettings settings = getSettings();

    for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
        HotkeyConfig& config = it.value();

        // Load key sequence from settings, fallback to default
        QString storedValue = settings.value(config.settingsKey, config.defaultKeySequence).toString();
        config.keySequence = storedValue;

        // Load enabled state (stored as "<key>_enabled")
        QString enabledKey = config.settingsKey + QStringLiteral("_enabled");
        config.enabled = settings.value(enabledKey, true).toBool();

        // Update status based on configuration
        if (config.keySequence.isEmpty()) {
            config.status = HotkeyStatus::Unset;
        } else if (!config.enabled) {
            config.status = HotkeyStatus::Disabled;
        }
    }
}

void HotkeyManager::saveToSettings(HotkeyAction action)
{
    auto it = m_configs.find(action);
    if (it == m_configs.end()) {
        return;
    }

    const HotkeyConfig& config = it.value();
    QSettings settings = getSettings();

    settings.setValue(config.settingsKey, config.keySequence);

    QString enabledKey = config.settingsKey + QStringLiteral("_enabled");
    settings.setValue(enabledKey, config.enabled);
}

HotkeyConfig HotkeyManager::getConfig(HotkeyAction action) const
{
    return m_configs.value(action);
}

QList<HotkeyConfig> HotkeyManager::getAllConfigs() const
{
    return m_configs.values();
}

QList<HotkeyConfig> HotkeyManager::getConfigsByCategory(HotkeyCategory category) const
{
    QList<HotkeyConfig> result;
    for (const auto& config : m_configs) {
        if (config.category == category) {
            result.append(config);
        }
    }
    return result;
}

QString HotkeyManager::formatKeySequence(const QString& keySequence)
{
    if (keySequence.isEmpty()) {
        return QString();
    }

    // Handle native keycode format
    if (keySequence.startsWith(QStringLiteral("Native:"), Qt::CaseInsensitive)) {
        return keySequence;  // Display as-is for native codes
    }

    // Use Qt's native text formatting for standard sequences
    QKeySequence seq(keySequence);
    return seq.toString(QKeySequence::NativeText);
}

bool HotkeyManager::updateHotkey(HotkeyAction action, const QString& keySequence)
{
    auto it = m_configs.find(action);
    if (it == m_configs.end()) {
        return false;
    }

    HotkeyConfig& config = it.value();
    QString oldSequence = config.keySequence;

    // Unregister old hotkey if it exists
    unregisterHotkey(action);

    // Update configuration
    config.keySequence = keySequence;

    if (keySequence.isEmpty()) {
        // Clearing the hotkey
        config.status = HotkeyStatus::Unset;
        saveToSettings(action);
        emit hotkeyChanged(action, config);
        emit registrationStatusChanged(action, config.status);
        return true;
    }

    // Try to register the new hotkey
    if (config.enabled && registerHotkey(action)) {
        config.status = HotkeyStatus::Registered;
        saveToSettings(action);
        emit hotkeyChanged(action, config);
        emit registrationStatusChanged(action, config.status);
        return true;
    }

    // Registration failed - revert to old sequence and restore runtime status.
    config.keySequence = oldSequence;

    if (config.keySequence.isEmpty()) {
        config.status = HotkeyStatus::Unset;
    } else if (!config.enabled) {
        config.status = HotkeyStatus::Disabled;
    } else {
        const bool restored = registerHotkey(action);
        config.status = restored ? HotkeyStatus::Registered : HotkeyStatus::Failed;
    }

    emit hotkeyChanged(action, config);
    emit registrationStatusChanged(action, config.status);
    // Requested update failed, even if rollback restored the previous hotkey.
    return false;
}

bool HotkeyManager::setHotkeyEnabled(HotkeyAction action, bool enabled)
{
    auto it = m_configs.find(action);
    if (it == m_configs.end()) {
        return false;
    }

    HotkeyConfig& config = it.value();
    if (config.enabled == enabled) {
        return true;  // No change needed
    }

    config.enabled = enabled;

    if (enabled && !config.keySequence.isEmpty()) {
        // Enable: try to register
        if (registerHotkey(action)) {
            config.status = HotkeyStatus::Registered;
        } else {
            config.status = HotkeyStatus::Failed;
        }
    } else if (!enabled) {
        // Disable: unregister
        unregisterHotkey(action);
        config.status = HotkeyStatus::Disabled;
    }

    saveToSettings(action);
    emit hotkeyChanged(action, config);
    emit registrationStatusChanged(action, config.status);
    return true;
}

bool HotkeyManager::resetToDefault(HotkeyAction action)
{
    auto it = m_configs.find(action);
    if (it == m_configs.end()) {
        return false;
    }

    HotkeyConfig& config = it.value();
    config.enabled = true;
    return updateHotkey(action, config.defaultKeySequence);
}

void HotkeyManager::resetAllToDefaults()
{
    for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
        resetToDefault(it.key());
    }
}

void HotkeyManager::suspendRegistration()
{
    for (QHotkey* hotkey : m_hotkeys) {
        if (hotkey && hotkey->isRegistered()) {
            hotkey->setRegistered(false);
        }
    }
}

void HotkeyManager::resumeRegistration()
{
    for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
        const HotkeyConfig& config = it.value();
        if (!config.keySequence.isEmpty() && config.enabled) {
            QHotkey* hotkey = m_hotkeys.value(it.key());
            if (hotkey) {
                hotkey->setRegistered(true);
            }
        }
    }
}

std::optional<HotkeyAction> HotkeyManager::hasConflict(const QString& keySequence,
                                                        std::optional<HotkeyAction> excludeAction) const
{
    if (keySequence.isEmpty()) {
        return std::nullopt;
    }

    QString normalizedSequence = keySequence.toLower().simplified();

    for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
        if (excludeAction && it.key() == *excludeAction) {
            continue;
        }

        const HotkeyConfig& config = it.value();
        if (config.keySequence.isEmpty() || !config.enabled) {
            continue;
        }

        QString otherNormalized = config.keySequence.toLower().simplified();
        if (normalizedSequence == otherNormalized) {
            return it.key();
        }
    }

    return std::nullopt;
}

QString HotkeyManager::getConflictDescription(const QString& keySequence,
                                               std::optional<HotkeyAction> excludeAction) const
{
    auto conflict = hasConflict(keySequence, excludeAction);
    if (!conflict) {
        return QString();
    }

    auto it = m_configs.find(*conflict);
    if (it == m_configs.end()) {
        return QString();
    }

    return tr("Conflicts with: %1").arg(it.value().displayName);
}

bool HotkeyManager::registerHotkey(HotkeyAction action)
{
    auto configIt = m_configs.find(action);
    if (configIt == m_configs.end()) {
        return false;
    }

    HotkeyConfig& config = configIt.value();
    if (config.keySequence.isEmpty()) {
        return false;
    }

    // Remove existing instance if any
    unregisterHotkey(action);

    // Create new QHotkey instance
    createHotkeyInstance(action);

    QHotkey* hotkey = m_hotkeys.value(action);
    if (!hotkey) {
        return false;
    }

    return hotkey->isRegistered();
}

void HotkeyManager::unregisterHotkey(HotkeyAction action)
{
    QHotkey* hotkey = m_hotkeys.take(action);
    if (hotkey) {
        hotkey->setRegistered(false);
        delete hotkey;
    }
}

void HotkeyManager::unregisterAllHotkeys()
{
    for (QHotkey* hotkey : m_hotkeys) {
        if (hotkey) {
            hotkey->setRegistered(false);
            delete hotkey;
        }
    }
    m_hotkeys.clear();
}

void HotkeyManager::createHotkeyInstance(HotkeyAction action)
{
    auto configIt = m_configs.find(action);
    if (configIt == m_configs.end()) {
        return;
    }

    const HotkeyConfig& config = configIt.value();
    if (config.keySequence.isEmpty()) {
        return;
    }

    QHotkey* hotkey = nullptr;
    quint32 nativeKey = 0;

    if (isNativeKeyCode(config.keySequence, nativeKey)) {
        // Native keycode (e.g., "Native:0x2C" for Print Screen on Windows)
        hotkey = new QHotkey(QHotkey::NativeShortcut(nativeKey, 0), true, this);
    } else {
        // Standard Qt key sequence
        hotkey = new QHotkey(QKeySequence(config.keySequence), true, this);
    }

    if (hotkey) {
        // Connect activated signal with captured action
        connect(hotkey, &QHotkey::activated, this, [this, action]() {
            onHotkeyActivated(action);
        });

        m_hotkeys[action] = hotkey;
    }
}

void HotkeyManager::onHotkeyActivated(HotkeyAction action)
{
    emit actionTriggered(action);
}

bool HotkeyManager::isNativeKeyCode(const QString& keySequence, quint32& nativeKey) const
{
    if (keySequence.startsWith(QStringLiteral("Native:0x"), Qt::CaseInsensitive)) {
        bool ok = false;
        nativeKey = keySequence.mid(9).toUInt(&ok, 16);
        return ok && nativeKey > 0;
    }
    return false;
}

}  // namespace SnapTray
