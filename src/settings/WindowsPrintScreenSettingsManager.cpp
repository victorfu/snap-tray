#include "settings/WindowsPrintScreenSettingsManager.h"

#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace SnapTray {

namespace {
constexpr auto kPrintScreenSnippingRegistryPath =
    "HKEY_CURRENT_USER\\Control Panel\\Keyboard";
constexpr auto kPrintScreenSnippingValue =
    "PrintScreenKeyForSnippingEnabled";

#ifdef Q_OS_WIN
// Nudge the running session to re-read the keyboard control-panel settings so
// disabling the Print Screen -> Snipping Tool shortcut can take effect without
// a sign-out. Windows may still defer it to the next sign-in, so this is
// best-effort; the caller tells the user about that fallback.
void broadcastKeyboardSettingChange()
{
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"Control Panel\\Keyboard"),
        SMTO_ABORTIFHUNG, 5000, nullptr);
}
#endif
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

    if (isSnippingShortcutEnabled()) {
        return false;
    }

    // Skip the system-wide broadcast when redirected to a throwaway test key.
    if (m_registryPathForTests.isEmpty()) {
        broadcastKeyboardSettingChange();
    }
    return true;
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
