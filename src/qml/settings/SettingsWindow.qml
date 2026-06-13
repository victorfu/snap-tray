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

    function stackIndexForKey(key) {
        var keys = ["general", "hotkeys", "advanced", "watermark", "ocr", "recording", "files", "updates", "about"]
        for (var i = 0; i < keys.length; i++) {
            if (keys[i] === key) {
                return i
            }
        }
        return 0
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
            Loader {
                id: generalPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentKey === "general"
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: generalSettingsPage
            }

            Loader {
                id: hotkeyPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentKey === "hotkeys"
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: hotkeySettingsPage
            }

            Loader {
                id: advancedPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentKey === "advanced"
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: advancedSettingsPage
            }

            Loader {
                id: watermarkPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentKey === "watermark"
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: watermarkSettingsPage
            }

            Loader {
                id: ocrPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: settingsBackend.ocrSettingsVisible
                    && (sidebar.currentKey === "ocr"
                    || status === Loader.Ready
                    || status === Loader.Loading)
                sourceComponent: ocrSettingsPage
            }

            Loader {
                id: recordingPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: settingsBackend.recordingSupported
                    && (sidebar.currentKey === "recording"
                    || status === Loader.Ready
                    || status === Loader.Loading)
                sourceComponent: recordingSettingsPage
            }

            Loader {
                id: filesPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentKey === "files"
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: filesSettingsPage
            }

            Loader {
                id: updatesPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentKey === "updates"
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: updatesSettingsPage
            }

            Loader {
                id: aboutPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentKey === "about"
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: aboutSettingsPage
            }
        }
    }

    Component { id: generalSettingsPage; GeneralSettings {} }
    Component { id: hotkeySettingsPage; HotkeySettings {} }
    Component { id: advancedSettingsPage; AdvancedSettings {} }
    Component { id: watermarkSettingsPage; WatermarkSettings {} }
    Component { id: ocrSettingsPage; OCRSettings {} }
    Component { id: recordingSettingsPage; RecordingSettings {} }
    Component { id: filesSettingsPage; FilesSettings {} }
    Component { id: updatesSettingsPage; UpdatesSettings {} }
    Component { id: aboutSettingsPage; AboutSettings {} }
}
