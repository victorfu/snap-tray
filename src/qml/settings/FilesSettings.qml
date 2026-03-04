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
        spacing: 4

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
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: -0.2
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
            font.pixelSize: PrimitiveTokens.fontSizeCaption
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: -0.2
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            text: qsTr("Preview: ") + settingsBackend.filenamePreview
            color: SemanticTokens.textSecondary
            font.pixelSize: PrimitiveTokens.fontSizeCaption
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: -0.2
            wrapMode: Text.WordWrap
        }

        SettingsSection { title: qsTr("Save Behavior") }

        SettingsToggle {
            label: qsTr("Auto-save screenshots")
            checked: settingsBackend.autoSaveScreenshots
            onToggled: settingsBackend.autoSaveScreenshots = checked
        }

        SettingsToggle {
            label: qsTr("Auto-save recordings")
            checked: settingsBackend.autoSaveRecordings
            onToggled: settingsBackend.autoSaveRecordings = checked
        }
    }
}
