import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * FilesSettings: Save locations, filename format, auto-save behavior.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: ComponentTokens.settingsColumnSpacing

        SettingsSection { title: qsTr("Save Locations") }

        SettingsPathPicker {
            label: qsTr("Screenshots")
            path: settingsBackend.screenshotPath
            onBrowseClicked: settingsBackend.browseScreenshotPath()
        }

        SettingsPathPicker {
            label: qsTr("Recordings")
            path: settingsBackend.recordingPath
            onBrowseClicked: settingsBackend.browseRecordingPath()
        }

        SettingsSection { title: qsTr("Filename Format") }

        SettingsRow {
            label: qsTr("Template")

            Rectangle {
                width: parent ? parent.width : 200
                height: 30
                radius: ComponentTokens.inputRadius
                color: ComponentTokens.inputBackground
                border.width: 1
                border.color: templateInput.activeFocus
                    ? ComponentTokens.inputBorderFocus
                    : ComponentTokens.inputBorder
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined

                TextInput {
                    id: templateInput
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: TextInput.AlignVCenter
                    text: settingsBackend.filenameTemplate
                    color: SemanticTokens.textPrimary
                    selectionColor: ComponentTokens.inputSelectionBackground
                    selectedTextColor: ComponentTokens.inputSelectionText
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    font.letterSpacing: SemanticTokens.letterSpacingDefault
                    clip: true
                    selectByMouse: true
                    onTextChanged: settingsBackend.filenameTemplate = text
                }
            }
        }

        Text {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            text: qsTr("Tokens: {prefix} {type} {w} {h} {monitor} {windowTitle} {appName} {regionIndex} {ext} {#}\nDate tokens: {yyyyMMdd_HHmmss}, {yyyy-MM-dd_HH-mm-ss}, or {date}")
            color: SemanticTokens.textTertiary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            text: qsTr("Preview: ") + settingsBackend.filenamePreview
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            wrapMode: Text.WordWrap
        }

        SettingsSection { title: qsTr("Save Behavior") }

        SettingsToggle {
            label: qsTr("Auto-save screenshots")
            checked: settingsBackend.autoSaveScreenshots
            onToggled: function(checked) { settingsBackend.autoSaveScreenshots = checked }
        }

        SettingsToggle {
            label: qsTr("Auto-save recordings")
            checked: settingsBackend.autoSaveRecordings
            onToggled: function(checked) { settingsBackend.autoSaveRecordings = checked }
        }
    }
}
