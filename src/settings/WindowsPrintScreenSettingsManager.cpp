#include "settings/WindowsPrintScreenSettingsManager.h"

#include <QSettings>

namespace SnapTray {

namespace {
constexpr auto kPrintScreenSnippingRegistryPath =
    "HKEY_CURRENT_USER\\Control Panel\\Keyboard";
constexpr auto kPrintScreenSnippingValue =
    "PrintScreenKeyForSnippingEnabled";
}

WindowsPrintScreenSettingsManager& WindowsPrintScreenSettingsManager::instance()
{
    static WindowsPrintScreenSettingsManager manager;
    return manager;
}

bool WindowsPrintScreenSettingsManager::isSnippingShortcutEnabled() const
{
#ifdef Q_OS_WIN
    QSettings settings(registryPath(), QSettings::NativeFormat);
    return settings.value(QString::fromLatin1(kPrintScreenSnippingValue), 1).toInt() != 0;
#else
    return false;
#endif
}

bool WindowsPrintScreenSettingsManager::disableSnippingShortcut()
{
#ifdef Q_OS_WIN
    QSettings settings(registryPath(), QSettings::NativeFormat);
    settings.setValue(QString::fromLatin1(kPrintScreenSnippingValue), 0);
    settings.sync();
    if (settings.status() == QSettings::AccessError) {
        return false;
    }

    return !isSnippingShortcutEnabled();
#else
    return false;
#endif
}

void WindowsPrintScreenSettingsManager::setRegistryPathForTests(const QString& path)
{
    m_registryPathForTests = path;
}

void WindowsPrintScreenSettingsManager::clearRegistryPathForTests()
{
    m_registryPathForTests.clear();
}

QString WindowsPrintScreenSettingsManager::registryPath() const
{
    if (!m_registryPathForTests.isEmpty()) {
        return m_registryPathForTests;
    }

    return QString::fromLatin1(kPrintScreenSnippingRegistryPath);
}

} // namespace SnapTray
