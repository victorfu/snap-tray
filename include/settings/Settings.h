#pragma once

#include <QSettings>
#include "version.h"

namespace SnapTray {

inline constexpr const char* kOrganizationName = "Victor Fu";
inline constexpr const char* kApplicationName = SNAPTRAY_APP_NAME;

// Hotkey settings keys
inline constexpr const char* kSettingsKeyHotkey = "hotkey";
inline constexpr const char* kSettingsKeyScreenCanvasHotkey = "screenCanvasHotkey";
inline constexpr const char* kSettingsKeyPasteHotkey = "pasteHotkey";
inline constexpr const char* kSettingsKeyQuickPinHotkey = "quickPinHotkey";
inline constexpr const char* kSettingsKeyPinFromImageHotkey = "pinFromImageHotkey";
inline constexpr const char* kSettingsKeyPinHistoryHotkey = "pinHistoryHotkey";
inline constexpr const char* kSettingsKeyRecordFullScreenHotkey = "recordFullScreenHotkey";

// Hotkey default values
inline constexpr const char* kDefaultHotkey = "F2";
inline constexpr const char* kDefaultScreenCanvasHotkey = "Ctrl+F2";
inline constexpr const char* kDefaultPasteHotkey = "F3";
inline constexpr const char* kDefaultQuickPinHotkey = "Shift+F2";
inline constexpr const char* kDefaultPinFromImageHotkey = "";  // No default
inline constexpr const char* kDefaultPinHistoryHotkey = "";  // No default
inline constexpr const char* kDefaultRecordFullScreenHotkey = "";  // No default

inline QSettings getSettings()
{
    return QSettings(kOrganizationName, kApplicationName);
}

} // namespace SnapTray
