import QtQuick
import QtQuick.Layouts
import SnapTrayQml

/**
 * SettingsWindow: Root layout for the QML settings dialog.
 *
 * Three-column layout: sidebar navigation | 1px separator | content stack.
 */
Item {
    id: root

    // One Loader per page, in this order (#227). The sidebar decides which
    // pages are offered; unsupported ones are never loaded.
    readonly property var pageKeys: ["general", "capture", "pins", "recording", "saving", "ocr", "hotkeys", "updates", "about"]

    function stackIndexForKey(key) {
        var index = pageKeys.indexOf(key)
        return index >= 0 ? index : 0
    }

    // Opens a page by key, if the sidebar offers it.
    function selectPage(key) {
        for (var i = 0; i < sidebar.pages.length; i++) {
            if (sidebar.pages[i].key === key) {
                sidebar.currentIndex = i
                return true
            }
        }
        return false
    }

    function pageSupported(key) {
        if (key === "recording")
            return settingsBackend.recordingSupported
        if (key === "ocr")
            return settingsBackend.ocrSettingsVisible
        return true
    }

    // Background fill
    Rectangle {
        anchors.fill: parent
        color: SemanticTokens.backgroundPrimary
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        SettingsSidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: Math.min(
                ComponentTokens.settingsSidebarMaxWidth,
                Math.max(ComponentTokens.settingsSidebarMinWidth, root.width * 0.25)
            )
        }

        // Vertical separator
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: SemanticTokens.borderDefault
        }

        // Content area
        StackLayout {
            id: contentStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.stackIndexForKey(sidebar.currentKey)

            // Load pages on first selection so opening Settings stays responsive.
            // Order must match pageKeys.
            PageLoader { pageKey: "general"; sourceComponent: generalSettingsPage }
            PageLoader { pageKey: "capture"; sourceComponent: captureSettingsPage }
            PageLoader { pageKey: "pins"; sourceComponent: pinWindowSettingsPage }
            PageLoader { pageKey: "recording"; sourceComponent: recordingSettingsPage }
            PageLoader { pageKey: "saving"; sourceComponent: savingSettingsPage }
            PageLoader { pageKey: "ocr"; sourceComponent: ocrSettingsPage }
            PageLoader { pageKey: "hotkeys"; sourceComponent: hotkeySettingsPage }
            PageLoader { pageKey: "updates"; sourceComponent: updatesSettingsPage }
            PageLoader { pageKey: "about"; sourceComponent: aboutSettingsPage }
        }
    }

    component PageLoader: Loader {
        property string pageKey
        objectName: "settingsPageLoader_" + pageKey
        Layout.fillWidth: true
        Layout.fillHeight: true
        active: root.pageSupported(pageKey)
            && (sidebar.currentKey === pageKey
            || status === Loader.Ready
            || status === Loader.Loading)
    }

    Component { id: generalSettingsPage; GeneralSettings {} }
    Component { id: captureSettingsPage; CaptureSettings {} }
    Component { id: pinWindowSettingsPage; PinWindowSettings {} }
    Component { id: recordingSettingsPage; RecordingSettings {} }
    Component { id: savingSettingsPage; SavingSettings {} }
    Component { id: ocrSettingsPage; OCRSettings {} }
    Component { id: hotkeySettingsPage; HotkeySettings {} }
    Component { id: updatesSettingsPage; UpdatesSettings {} }
    Component { id: aboutSettingsPage; AboutSettings {} }
}
