import QtQuick
import QtQuick.Layouts
import SnapTrayQml

/**
 * SettingsWindow: Root layout for the QML settings dialog.
 *
 * Three-column layout: sidebar navigation | 1px separator | content stack.
 * Bottom bar with Save / Cancel buttons, separated by a 1px top border.
 */
Item {
    id: root

    // Background fill
    Rectangle {
        anchors.fill: parent
        color: SemanticTokens.backgroundPrimary
    }

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: bottomBar.height
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
            currentIndex: sidebar.currentIndex

            // Load pages on first selection so opening Settings stays responsive.
            Loader {
                id: generalPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 0
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: generalSettingsPage
            }

            Loader {
                id: hotkeyPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 1
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: hotkeySettingsPage
            }

            Loader {
                id: advancedPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 2
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: advancedSettingsPage
            }

            Loader {
                id: watermarkPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 3
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: watermarkSettingsPage
            }

            Loader {
                id: ocrPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 4
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: ocrSettingsPage
            }

            Loader {
                id: recordingPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 5
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: recordingSettingsPage
            }

            Loader {
                id: filesPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 6
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: filesSettingsPage
            }

            Loader {
                id: updatesPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 7
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: updatesSettingsPage
            }

            Loader {
                id: aboutPageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: sidebar.currentIndex === 8
                    || status === Loader.Ready
                    || status === Loader.Loading
                sourceComponent: aboutSettingsPage
            }
        }
    }

    // Bottom bar with Save / Cancel
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: ComponentTokens.settingsBottomBarHeight
        color: SemanticTokens.backgroundPrimary

        // Top border
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: SemanticTokens.borderDefault
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: ComponentTokens.settingsBottomBarPaddingH
            anchors.verticalCenter: parent.verticalCenter
            spacing: SemanticTokens.spacing8

            SettingsButton {
                text: qsTr("Cancel")
                onClicked: settingsBackend.cancel()
            }

            SettingsButton {
                text: qsTr("Save")
                primary: true
                onClicked: settingsBackend.save()
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
