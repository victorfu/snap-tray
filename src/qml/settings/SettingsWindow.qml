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

    // Save confirmation toast
    Toast {
        id: saveToast
        level: 0  // Success
        title: qsTr("Settings saved")
        anchorMode: 1  // ParentTopCenter
        z: 100
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        displayDuration: 800
        onHideFinished: settingsBackend.cancel()  // close window after toast
    }

    Connections {
        target: settingsBackend
        function onSettingsSaved() { saveToast.show() }
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

            GeneralSettings {}
            HotkeySettings {}
            AdvancedSettings {}
            WatermarkSettings {}
            OCRSettings {}
            RecordingSettings {}
            FilesSettings {}
            UpdatesSettings {}
            AboutSettings {}
        }
    }

    // Bottom bar with Save / Cancel
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 52
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
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

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
}
