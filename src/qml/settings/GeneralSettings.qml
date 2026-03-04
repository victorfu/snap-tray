import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * GeneralSettings: Start on login, language, appearance, permissions, CLI.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    function findLanguageIndex() {
        var langs = settingsBackend.availableLanguages
        var current = settingsBackend.language
        for (var i = 0; i < langs.length; i++) {
            if (langs[i].code === current) return i
        }
        return 0
    }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: 4

        SettingsToggle {
            label: qsTr("Start on login")
            checked: settingsBackend.startOnLogin
            onToggled: settingsBackend.startOnLogin = checked
        }

        SettingsSection { title: qsTr("Language") }

        SettingsCombo {
            label: qsTr("Display language")
            model: settingsBackend.availableLanguages
            currentIndex: root.findLanguageIndex()
            minimumPopupWidth: 200
            onActivated: function(index) {
                var langs = settingsBackend.availableLanguages
                if (index >= 0 && index < langs.length)
                    settingsBackend.language = langs[index].code
            }
        }

        SettingsSection { title: qsTr("Appearance") }

        SettingsCombo {
            label: qsTr("Toolbar style")
            model: [
                { text: qsTr("Dark"), value: 0 },
                { text: qsTr("Light"), value: 1 }
            ]
            currentIndex: settingsBackend.toolbarStyle
            onActivated: function(index) { settingsBackend.toolbarStyle = index }
        }

        // macOS permissions
        SettingsSection {
            title: qsTr("Permissions")
            visible: settingsBackend.isMacOS
        }

        SettingsRow {
            label: qsTr("Screen Recording")
            visible: settingsBackend.isMacOS

            Row {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: 8

                Text {
                    text: settingsBackend.isMacOS && settingsBackend.hasScreenRecordingPermission
                        ? qsTr("Granted") : qsTr("Not Granted")
                    color: settingsBackend.isMacOS && settingsBackend.hasScreenRecordingPermission
                        ? SemanticTokens.statusSuccess : SemanticTokens.statusError
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: -0.2
                    anchors.verticalCenter: parent.verticalCenter
                }

                SettingsButton {
                    text: qsTr("Open Settings")
                    onClicked: settingsBackend.openScreenRecordingSettings()
                }
            }
        }

        SettingsRow {
            label: qsTr("Accessibility")
            visible: settingsBackend.isMacOS

            Row {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: 8

                Text {
                    text: settingsBackend.isMacOS && settingsBackend.hasAccessibilityPermission
                        ? qsTr("Granted") : qsTr("Not Granted")
                    color: settingsBackend.isMacOS && settingsBackend.hasAccessibilityPermission
                        ? SemanticTokens.statusSuccess : SemanticTokens.statusError
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: -0.2
                    anchors.verticalCenter: parent.verticalCenter
                }

                SettingsButton {
                    text: qsTr("Open Settings")
                    onClicked: settingsBackend.openAccessibilitySettings()
                }
            }
        }

        SettingsSection { title: qsTr("Command Line") }

        SettingsRow {
            label: qsTr("CLI Status")

            Row {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: 8

                Text {
                    text: settingsBackend.cliInstalled
                        ? qsTr("'snaptray' command is available")
                        : qsTr("'snaptray' command is not installed")
                    color: SemanticTokens.textSecondary
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: -0.2
                    anchors.verticalCenter: parent.verticalCenter
                }

                SettingsButton {
                    text: settingsBackend.cliInstalled
                        ? qsTr("Uninstall CLI") : qsTr("Install CLI")
                    onClicked: {
                        if (settingsBackend.cliInstalled)
                            settingsBackend.uninstallCLI()
                        else
                            settingsBackend.installCLI()
                    }
                }
            }
        }
    }
}
