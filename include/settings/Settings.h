#pragma once

#include <QSettings>

namespace SnapTray {

inline constexpr const char* kOrganizationName = "Victor Fu";
inline constexpr const char* kApplicationName = "SnapTray";

// Hotkey settings keys
inline constexpr const char* kSettingsKeyHotkey = "hotkey";
inline constexpr const char* kSettingsKeyScreenCanvasHotkey = "screenCanvasHotkey";
inline constexpr const char* kSettingsKeyPasteHotkey = "pasteHotkey";
inline constexpr const char* kSettingsKeyQuickPinHotkey = "quickPinHotkey";

// Hotkey default values
inline constexpr const char* kDefaultHotkey = "F2";
inline constexpr const char* kDefaultScreenCanvasHotkey = "Ctrl+F2";
inline constexpr const char* kDefaultPasteHotkey = "F3";
inline constexpr const char* kDefaultQuickPinHotkey = "Shift+F2";

inline QSettings getSettings()
{
    return QSettings(kOrganizationName, kApplicationName);
}

} // namespace SnapTray
