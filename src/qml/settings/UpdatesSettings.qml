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

    Toast {
        id: updateToast
        level: 3 // Info
        anchorMode: 1 // ParentTopCenter
        fixedWidth: true
        z: 10
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
    }

    Connections {
        target: settingsBackend

        function onUpdateAvailable(version, notes) {
            updateToast.level = 0 // Success
            updateToast.title = qsTr("Update available")
            updateToast.message = qsTr("Version %1 is available.").arg(version)
            updateToast.show()
        }

        function onNoUpdateAvailable() {
            updateToast.level = 3 // Info
            updateToast.title = qsTr("You're up to date")
            updateToast.message = qsTr("No updates are currently available.")
            updateToast.show()
        }

        function onUpdateCheckFailed(error) {
            updateToast.level = 1 // Error
            updateToast.title = qsTr("Update check failed")
            updateToast.message = error
            updateToast.show()
        }
    }

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
                font.pixelSize: PrimitiveTokens.fontSizeBody
                font.family: PrimitiveTokens.fontFamily
                font.letterSpacing: PrimitiveTokens.letterSpacingTight
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            }
        }

        SettingsSection { title: qsTr("Automatic Updates") }

        SettingsToggle {
            label: qsTr("Auto-check updates")
            checked: settingsBackend.autoCheckUpdates
            onToggled: settingsBackend.autoCheckUpdates = checked
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

        Spacer { size: PrimitiveTokens.spacing16 }

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
            font.pixelSize: PrimitiveTokens.fontSizeCaption
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: PrimitiveTokens.letterSpacingTight
            horizontalAlignment: Text.AlignHCenter
        }

        Spacer { size: PrimitiveTokens.spacing12 }

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
