import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * UpdatesSettings: Version info, auto-update preferences, manual check.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    readonly property var frequencyHours: [24, 72, 168, 336, 720]

    function frequencyToIndex(hours) {
        for (var i = 0; i < frequencyHours.length; i++) {
            if (frequencyHours[i] === hours) return i
        }
        return 0
    }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: ComponentTokens.settingsColumnSpacing

        SettingsSection { title: qsTr("Version") }

        SettingsRow {
            label: qsTr("Current Version")

            Text {
                text: settingsBackend.currentVersion
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
                font.letterSpacing: SemanticTokens.letterSpacingDefault
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            }
        }

        SettingsSection { title: qsTr("Automatic Updates") }

        SettingsToggle {
            label: qsTr("Auto-check updates")
            checked: settingsBackend.autoCheckUpdates
            onToggled: function(checked) { settingsBackend.autoCheckUpdates = checked }
        }

        SettingsCombo {
            label: qsTr("Check frequency")
            model: [
                { text: qsTr("Every day"), value: 24 },
                { text: qsTr("Every 3 days"), value: 72 },
                { text: qsTr("Every week"), value: 168 },
                { text: qsTr("Every 2 weeks"), value: 336 },
                { text: qsTr("Every month"), value: 720 }
            ]
            currentIndex: root.frequencyToIndex(settingsBackend.checkFrequencyHours)
            enabled: settingsBackend.autoCheckUpdates
            onActivated: function(index) {
                settingsBackend.checkFrequencyHours = root.frequencyHours[index]
            }
        }

        Spacer { size: SemanticTokens.spacing16 }

        // Separator
        Rectangle {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: 1
            color: SemanticTokens.borderDefault
        }

        Spacer {}

        // Last checked text
        Text {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            text: settingsBackend.lastCheckedText
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            horizontalAlignment: Text.AlignHCenter
        }

        Spacer { size: SemanticTokens.spacing12 }

        // Check Now button (centered)
        Item {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: 36

            SettingsButton {
                anchors.horizontalCenter: parent.horizontalCenter
                text: settingsBackend.isCheckingForUpdates
                    ? qsTr("Checking...") : qsTr("Check Now")
                primary: true
                enabled: !settingsBackend.isCheckingForUpdates
                onClicked: settingsBackend.checkForUpdates()
            }
        }
    }
}
