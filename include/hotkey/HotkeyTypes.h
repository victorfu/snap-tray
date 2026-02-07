/**
 * @file HotkeyTypes.h
 * @brief Hotkey system type definitions
 *
 * Defines all hotkey-related enums, structs, and compile-time metadata.
 * Uses category-based numbering for extensibility.
 */

#pragma once

#include <QString>
#include <QObject>

namespace SnapTray {

/**
 * @brief Hotkey action identifiers with category-based numbering.
 *
 * Categories:
 * - Capture: 100-199
 * - Canvas: 200-299
 * - Clipboard: 300-399
 * - Pin: 400-499
 * - Recording: 500-599
 * - Tools: 600-699 (reserved for future)
 * - App: 900-999 (reserved for future)
 */
enum class HotkeyAction {
    None = 0,

    // Capture actions (100-199)
    RegionCapture = 100,

    // Canvas actions (200-299)
    ScreenCanvas = 200,

    // Clipboard actions (300-399)
    PasteFromClipboard = 300,

    // Pin actions (400-499)
    QuickPin = 400,
    PinFromImage = 401,
    PinHistory = 402,

    // Recording actions (500-599)
    RecordFullScreen = 500,
};

/**
 * @brief Hotkey category for UI grouping.
 */
enum class HotkeyCategory {
    Capture,
    Canvas,
    Clipboard,
    Pin,
    Recording,
};

/**
 * @brief Hotkey registration status.
 */
enum class HotkeyStatus {
    Unset,      ///< No hotkey configured (empty key sequence)
    Registered, ///< Successfully registered with the OS
    Failed,     ///< Registration failed (conflict with OS or other app)
    Disabled,   ///< User explicitly disabled this hotkey
};

/**
 * @brief Runtime configuration for a single hotkey.
 */
struct HotkeyConfig {
    HotkeyAction action = HotkeyAction::None;
    HotkeyCategory category = HotkeyCategory::Capture;
    QString keySequence;        ///< Current key sequence (e.g., "F2", "Ctrl+F2", "Native:0x2C")
    QString displayName;        ///< User-visible name (e.g., "Region Capture")
    QString description;        ///< Tooltip/help text
    QString settingsKey;        ///< QSettings key for persistence
    QString defaultKeySequence; ///< Default value for reset
    bool enabled = true;        ///< Whether the hotkey is active
    HotkeyStatus status = HotkeyStatus::Unset;

    bool isOptional() const { return defaultKeySequence.isEmpty(); }
    bool isEmpty() const { return keySequence.isEmpty(); }
    bool isModified() const { return keySequence != defaultKeySequence; }
};

/**
 * @brief Compile-time metadata for hotkey definitions.
 */
struct HotkeyMetadata {
    HotkeyAction action;
    HotkeyCategory category;
    const char* displayName;
    const char* description;
    const char* settingsKey;
    const char* defaultKeySequence;
};

/**
 * @brief Default hotkey definitions mapping to existing Settings.h keys.
 *
 * This array defines all supported hotkeys with their metadata.
 * The settingsKey values match existing keys in Settings.h for backward compatibility.
 */
inline constexpr HotkeyMetadata kDefaultHotkeys[] = {
    {
        HotkeyAction::RegionCapture,
        HotkeyCategory::Capture,
        "Region Capture",
        "Capture a selected region of the screen",
        "hotkey",  // kSettingsKeyHotkey
        "F2"       // kDefaultHotkey
    },
    {
        HotkeyAction::ScreenCanvas,
        HotkeyCategory::Canvas,
        "Screen Canvas",
        "Annotate directly on the screen",
        "screenCanvasHotkey",  // kSettingsKeyScreenCanvasHotkey
        "Ctrl+F2"              // kDefaultScreenCanvasHotkey
    },
    {
        HotkeyAction::PasteFromClipboard,
        HotkeyCategory::Clipboard,
        "Paste",
        "Pin content from clipboard",
        "pasteHotkey",  // kSettingsKeyPasteHotkey
        "F3"            // kDefaultPasteHotkey
    },
    {
        HotkeyAction::QuickPin,
        HotkeyCategory::Pin,
        "Quick Pin",
        "Select and pin a region directly",
        "quickPinHotkey",  // kSettingsKeyQuickPinHotkey
        "Shift+F2"         // kDefaultQuickPinHotkey
    },
    {
        HotkeyAction::PinFromImage,
        HotkeyCategory::Pin,
        "Pin from Image",
        "Pin an image from file",
        "pinFromImageHotkey",  // kSettingsKeyPinFromImageHotkey
        ""                     // Optional, no default
    },
    {
        HotkeyAction::PinHistory,
        HotkeyCategory::Pin,
        "Pin History",
        "Open pin history window",
        "pinHistoryHotkey",  // kSettingsKeyPinHistoryHotkey
        ""                   // Optional, no default
    },
    {
        HotkeyAction::RecordFullScreen,
        HotkeyCategory::Recording,
        "Record Full Screen",
        "Start full screen recording",
        "recordFullScreenHotkey",  // kSettingsKeyRecordFullScreenHotkey
        ""                         // Optional, no default
    },
};

inline constexpr size_t kDefaultHotkeyCount = sizeof(kDefaultHotkeys) / sizeof(kDefaultHotkeys[0]);

/**
 * @brief Get the category for a given action.
 */
inline HotkeyCategory getCategoryForAction(HotkeyAction action)
{
    int value = static_cast<int>(action);
    if (value >= 100 && value < 200) return HotkeyCategory::Capture;
    if (value >= 200 && value < 300) return HotkeyCategory::Canvas;
    if (value >= 300 && value < 400) return HotkeyCategory::Clipboard;
    if (value >= 400 && value < 500) return HotkeyCategory::Pin;
    if (value >= 500 && value < 600) return HotkeyCategory::Recording;
    return HotkeyCategory::Capture;  // Default fallback
}

/**
 * @brief Get the display name for a category.
 */
inline QString getCategoryDisplayName(HotkeyCategory category)
{
    switch (category) {
    case HotkeyCategory::Capture:   return QObject::tr("Capture");
    case HotkeyCategory::Canvas:    return QObject::tr("Canvas");
    case HotkeyCategory::Clipboard: return QObject::tr("Clipboard");
    case HotkeyCategory::Pin:       return QObject::tr("Pin");
    case HotkeyCategory::Recording: return QObject::tr("Recording");
    default: return QString();
    }
}

/**
 * @brief Get the icon/emoji for a category (for UI display).
 */
inline QString getCategoryIcon(HotkeyCategory category)
{
    switch (category) {
    case HotkeyCategory::Capture:   return QString::fromUtf8("\xF0\x9F\x93\xB8");  // Camera
    case HotkeyCategory::Canvas:    return QString::fromUtf8("\xF0\x9F\x8E\xA8");  // Palette
    case HotkeyCategory::Clipboard: return QString::fromUtf8("\xF0\x9F\x93\x8B");  // Clipboard
    case HotkeyCategory::Pin:       return QString::fromUtf8("\xF0\x9F\x93\x8C");  // Pushpin
    case HotkeyCategory::Recording: return QString::fromUtf8("\xF0\x9F\x8E\xAC");  // Clapper
    default: return QString();
    }
}

/**
 * @brief Get the display name for a status.
 */
inline QString getStatusDisplayName(HotkeyStatus status)
{
    switch (status) {
    case HotkeyStatus::Unset:      return QObject::tr("Not Set");
    case HotkeyStatus::Registered: return QObject::tr("Active");
    case HotkeyStatus::Failed:     return QObject::tr("Conflict");
    case HotkeyStatus::Disabled:   return QObject::tr("Disabled");
    default: return QString();
    }
}

}  // namespace SnapTray
