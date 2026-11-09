#pragma once

// Element type classification for detected UI elements.
// Kept in its own header so pure-data code can use it without pulling in WindowDetector.
enum class ElementType {
    Window,         // Normal application window
    ContextMenu,    // Right-click context menu
    PopupMenu,      // Application menu dropdown
    Dialog,         // Dialog/modal window
    StatusBarItem,  // Menu bar popup (macOS) / System tray popup (Windows)
    Unknown
};
