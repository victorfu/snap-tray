/**
 * @file HotkeyManager.cpp
 * @brief HotkeyManager implementation
 */

#include "hotkey/HotkeyManager.h"
#include "settings/Settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHotkey>
#include <QKeySequence>
#include <QSettings>

#include <algorithm>

namespace SnapTray {

namespace {
QString translateHotkeyText(const char* sourceText)
{
    const QString source = QString::fromUtf8(sourceText);

    // Prefer the new namespaced context used by QT_TRANSLATE_NOOP entries.
    QString translated = QCoreApplication::translate("SnapTray::HotkeyManager", sourceText);
    if (translated != source) {
        return translated;
    }

    // Backward compatibility: existing .ts files still use "HotkeyManager".
    translated = QCoreApplication::translate("HotkeyManager", sourceText);
    if (translated != source) {
        return translated;
    }

    return source;
}

QString normalizeKeySequence(const QString& keySequence)
{
    return keySequence.toLower().simplified();
}
}  // namespace

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
        refreshStatusAndRegistration(it.key());

        const HotkeyConfig& config = it.value();
        if (config.status == HotkeyStatus::Failed) {
            failedHotkeys << config.displayName;
        }
    }

    // Notify upper layer about initialization result.
    if (!failedHotkeys.isEmpty()) {
        qWarning() << "HotkeyManager: failed to register hotkeys:" << failedHotkeys;
    }

    m_initialized = true;
    emit initializationCompleted(failedHotkeys);
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
    m_registrationOrders.clear();
    m_nextRegistrationOrder = 1;

    for (size_t i = 0; i < kDefaultHotkeyCount; ++i) {
        const auto& meta = kDefaultHotkeys[i];

        HotkeyConfig config;
        config.action = meta.action;
        config.category = meta.category;
        config.displayName = translateHotkeyText(meta.displayName);
        config.description = translateHotkeyText(meta.description);
        config.settingsKey = QString::fromUtf8(meta.settingsKey);
        config.defaultKeySequence = QString::fromUtf8(meta.defaultKeySequence);
        config.keySequence = config.defaultKeySequence;
        config.enabled = true;
        config.status = HotkeyStatus::Unset;

        m_configs[meta.action] = config;
        m_registrationOrders[meta.action] = 0;
    }
}

void HotkeyManager::loadFromSettings()
{
    QSettings settings = getSettings();
    quint64 highestRegistrationOrder = 0;

    for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
        HotkeyConfig& config = it.value();

        // Load key sequence from settings, fallback to default
        QString storedValue = settings.value(config.settingsKey, config.defaultKeySequence).toString();
        config.keySequence = storedValue;

        // Load enabled state (stored as "<key>_enabled")
        const QString enabledKey = enabledSettingsKey(config);
        config.enabled = settings.value(enabledKey, true).toBool();

        const QString registrationOrderKey = registrationOrderSettingsKey(config);
        const quint64 registrationOrderValue =
            settings.value(registrationOrderKey, 0).toULongLong();
        m_registrationOrders[it.key()] = registrationOrderValue;
        highestRegistrationOrder = std::max(highestRegistrationOrder, registrationOrderValue);

        // Update status based on configuration
        if (config.keySequence.isEmpty()) {
            config.status = HotkeyStatus::Unset;
        } else if (!config.enabled) {
            config.status = HotkeyStatus::Disabled;
        } else {
            config.status = HotkeyStatus::Failed;
        }
    }

    const quint64 storedNextRegistrationOrder =
        settings.value(QString::fromLatin1(kSettingsKeyHotkeyRegistrationCounter), 1).toULongLong();
    m_nextRegistrationOrder = std::max<quint64>(
        std::max<quint64>(storedNextRegistrationOrder, highestRegistrationOrder + 1), 1);
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

    const QString enabledKey = enabledSettingsKey(config);
    settings.setValue(enabledKey, config.enabled);

    const QString registrationOrderKey = registrationOrderSettingsKey(config);
    const quint64 order = registrationOrder(action);
    if (order == 0) {
        settings.remove(registrationOrderKey);
    } else {
        settings.setValue(registrationOrderKey, order);
    }
    settings.setValue(QString::fromLatin1(kSettingsKeyHotkeyRegistrationCounter),
                      m_nextRegistrationOrder);
}

QString HotkeyManager::enabledSettingsKey(const HotkeyConfig& config) const
{
    return config.settingsKey + QString::fromLatin1(kSettingsKeyHotkeyEnabledSuffix);
}

QString HotkeyManager::registrationOrderSettingsKey(const HotkeyConfig& config) const
{
    return config.settingsKey + QString::fromLatin1(kSettingsKeyHotkeyRegistrationOrderSuffix);
}

quint64 HotkeyManager::registrationOrder(HotkeyAction action) const
{
    return m_registrationOrders.value(action, 0);
}

bool HotkeyManager::hasHigherRegistrationPriority(HotkeyAction lhs, HotkeyAction rhs) const
{
    const quint64 lhsOrder = registrationOrder(lhs);
    const quint64 rhsOrder = registrationOrder(rhs);
    if (lhsOrder != rhsOrder) {
        return lhsOrder > rhsOrder;
    }

    return static_cast<int>(lhs) < static_cast<int>(rhs);
}

std::optional<HotkeyAction> HotkeyManager::preferredConflictOwner(HotkeyAction action) const
{
    const auto configIt = m_configs.find(action);
    if (configIt == m_configs.end()) {
        return std::nullopt;
    }

    const HotkeyConfig& config = configIt.value();
    if (config.keySequence.isEmpty() || !config.enabled) {
        return std::nullopt;
    }

    const QString normalizedSequence = normalizeKeySequence(config.keySequence);
    HotkeyAction preferredOwner = action;

    for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
        if (it.key() == action) {
            continue;
        }

        const HotkeyConfig& other = it.value();
        if (other.keySequence.isEmpty() || !other.enabled) {
            continue;
        }

        if (normalizeKeySequence(other.keySequence) != normalizedSequence) {
            continue;
        }

        if (hasHigherRegistrationPriority(it.key(), preferredOwner)) {
            preferredOwner = it.key();
        }
    }

    if (preferredOwner == action) {
        return std::nullopt;
    }

    return preferredOwner;
}

void HotkeyManager::promoteRegistrationOrder(HotkeyAction action)
{
    m_registrationOrders[action] = m_nextRegistrationOrder++;
}

void HotkeyManager::clearRegistrationOrder(HotkeyAction action)
{
    m_registrationOrders[action] = 0;
}

void HotkeyManager::refreshStatusAndRegistration(HotkeyAction action, bool emitSignals)
{
    auto it = m_configs.find(action);
    if (it == m_configs.end()) {
        return;
    }

    HotkeyConfig& config = it.value();

    unregisterHotkey(action);

    if (config.keySequence.isEmpty()) {
        config.status = HotkeyStatus::Unset;
    } else if (!config.enabled) {
        config.status = HotkeyStatus::Disabled;
    } else if (preferredConflictOwner(action).has_value()) {
        config.status = HotkeyStatus::Failed;
    } else if (registerHotkey(action)) {
        config.status = HotkeyStatus::Registered;
        if (registrationOrder(action) == 0) {
            promoteRegistrationOrder(action);
            saveToSettings(action);
        }
    } else {
        config.status = HotkeyStatus::Failed;
    }

    if (emitSignals) {
        emit hotkeyChanged(action, config);
        emit registrationStatusChanged(action, config.status);
    }
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

    // Unregister old hotkey if it exists
    unregisterHotkey(action);

    // Update configuration
    config.keySequence = keySequence;

    if (keySequence.isEmpty()) {
        // Clearing the hotkey
        config.status = HotkeyStatus::Unset;
        clearRegistrationOrder(action);
        saveToSettings(action);
        emit hotkeyChanged(action, config);
        emit registrationStatusChanged(action, config.status);
        return true;
    }

    if (!config.enabled) {
        config.status = HotkeyStatus::Disabled;
        clearRegistrationOrder(action);
        saveToSettings(action);
        emit hotkeyChanged(action, config);
        emit registrationStatusChanged(action, config.status);
        return true;
    }

    const bool hasInternalConflict = preferredConflictOwner(action).has_value();
    const bool registered = !hasInternalConflict && registerHotkey(action);
    if (registered) {
        config.status = HotkeyStatus::Registered;
        promoteRegistrationOrder(action);
    } else {
        config.status = HotkeyStatus::Failed;
        clearRegistrationOrder(action);
    }

    saveToSettings(action);
    emit hotkeyChanged(action, config);
    emit registrationStatusChanged(action, config.status);
    return registered;
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
        const bool hasInternalConflict = preferredConflictOwner(action).has_value();
        if (!hasInternalConflict && registerHotkey(action)) {
            config.status = HotkeyStatus::Registered;
            promoteRegistrationOrder(action);
        } else {
            config.status = HotkeyStatus::Failed;
            clearRegistrationOrder(action);
        }
    } else if (!enabled) {
        // Disable: unregister
        unregisterHotkey(action);
        config.status = HotkeyStatus::Disabled;
        clearRegistrationOrder(action);
    } else {
        config.status = HotkeyStatus::Unset;
        clearRegistrationOrder(action);
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

QStringList HotkeyManager::resetAllToDefaults()
{
    const QList<HotkeyAction> actions = m_configs.keys();

    unregisterAllHotkeys();
    for (HotkeyAction action : actions) {
        auto it = m_configs.find(action);
        if (it == m_configs.end()) {
            continue;
        }

        HotkeyConfig& config = it.value();
        config.enabled = true;
        config.keySequence = config.defaultKeySequence;
        clearRegistrationOrder(action);
        saveToSettings(action);
    }

    QStringList failedHotkeys;
    for (HotkeyAction action : actions) {
        refreshStatusAndRegistration(action);

        const HotkeyConfig& config = m_configs.value(action);
        saveToSettings(action);
        emit hotkeyChanged(action, config);
        emit registrationStatusChanged(action, config.status);

        if (config.status == HotkeyStatus::Failed) {
            failedHotkeys << config.displayName;
        }
    }

    return failedHotkeys;
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

    const QString normalizedSequence = normalizeKeySequence(keySequence);
    std::optional<HotkeyAction> preferredConflict;

    for (auto it = m_configs.begin(); it != m_configs.end(); ++it) {
        if (excludeAction && it.key() == *excludeAction) {
            continue;
        }

        const HotkeyConfig& config = it.value();
        if (config.keySequence.isEmpty() || !config.enabled) {
            continue;
        }

        const QString otherNormalized = normalizeKeySequence(config.keySequence);
        if (normalizedSequence == otherNormalized) {
            if (!preferredConflict || hasHigherRegistrationPriority(it.key(), *preferredConflict)) {
                preferredConflict = it.key();
            }
        }
    }

    return preferredConflict;
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
