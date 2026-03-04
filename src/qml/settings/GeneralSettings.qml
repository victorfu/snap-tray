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
        spacing: ComponentTokens.settingsColumnSpacing

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

        SettingsPermissionRow {
            label: qsTr("Screen Recording")
            description: qsTr("Required for capturing screenshots and recording your screen.")
            visible: settingsBackend.isMacOS
            granted: settingsBackend.isMacOS && settingsBackend.hasScreenRecordingPermission
            onOpenSettingsClicked: settingsBackend.openScreenRecordingSettings()
        }

        SettingsPermissionRow {
            label: qsTr("Accessibility")
            description: qsTr("Required for global hotkeys and window detection.")
            visible: settingsBackend.isMacOS
            granted: settingsBackend.isMacOS && settingsBackend.hasAccessibilityPermission
            onOpenSettingsClicked: settingsBackend.openAccessibilitySettings()
        }

        SettingsSection { title: qsTr("Command Line") }

        SettingsRow {
            id: cliRow
            label: qsTr("CLI Status")

            property bool busy: false

            Connections {
                target: settingsBackend
                function onCliInstalledChanged() { cliRow.busy = false }
            }

            Row {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: 8

                BusySpinner {
                    running: cliRow.busy
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: cliRow.busy
                        ? qsTr("Please wait...")
                        : settingsBackend.cliInstalled
                            ? qsTr("'snaptray' command is available")
                            : qsTr("'snaptray' command is not installed")
                    color: SemanticTokens.textSecondary
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
                    anchors.verticalCenter: parent.verticalCenter
                }

                SettingsButton {
                    text: settingsBackend.cliInstalled
                        ? qsTr("Uninstall CLI") : qsTr("Install CLI")
                    enabled: !cliRow.busy
                    onClicked: {
                        cliRow.busy = true
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
