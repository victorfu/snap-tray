import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SnapTrayQml

/**
 * SavingSettings (#227): where files go, their names, the watermark on them,
 * and how many screenshots the Library keeps. Was the Files and Watermark pages.
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

        SettingsPageHeader {
            title: qsTr("Saving")
            description: qsTr("Where screenshots and recordings are saved, how they are named, and the watermark added to them.")
        }

        SettingsSection { title: qsTr("Save Locations") }

        SettingsPathPicker {
            resetKey: "screenshotPath"
            objectName: "screenshotPathPicker"
            label: qsTr("Screenshots")
            path: settingsBackend.screenshotPath
            onBrowseClicked: settingsBackend.browseScreenshotPath()
        }

        SettingsPathPicker {
            resetKey: "recordingPath"
            label: qsTr("Recordings")
            visible: settingsBackend.recordingSupported
            path: settingsBackend.recordingPath
            onBrowseClicked: settingsBackend.browseRecordingPath()
        }

        SettingsSection { title: qsTr("Filename Format") }

        SettingsRow {
            resetKey: "filenameTemplate"
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
            resetKey: "autoSaveScreenshots"
            label: qsTr("Auto-save screenshots")
            checked: settingsBackend.autoSaveScreenshots
            onToggled: function(checked) { settingsBackend.autoSaveScreenshots = checked }
        }

        SettingsToggle {
            resetKey: "useLastScreenshotSaveLocation"
            objectName: "rememberLastFolderToggle"
            label: qsTr("Remember last folder")
            description: qsTr("When saving an image manually, the dialog opens in the folder of the last successful save. Auto-save still uses the Screenshots folder above.")
            checked: settingsBackend.useLastScreenshotSaveLocation
            onToggled: function(checked) { settingsBackend.useLastScreenshotSaveLocation = checked }
        }

        SettingsToggle {
            resetKey: "autoSaveRecordings"
            label: qsTr("Auto-save recordings")
            visible: settingsBackend.recordingSupported
            checked: settingsBackend.autoSaveRecordings
            onToggled: function(checked) { settingsBackend.autoSaveRecordings = checked }
        }

        SettingsSection { title: qsTr("Watermark") }

        SettingsToggle {
            resetKey: "watermarkEnabled"
            objectName: "watermarkImagesToggle"
            label: qsTr("Apply to images")
            checked: settingsBackend.watermarkEnabled
            onToggled: function(checked) { settingsBackend.watermarkEnabled = checked }
        }

        SettingsToggle {
            resetKey: "watermarkApplyToRecording"
            label: qsTr("Apply to recordings")
            visible: settingsBackend.recordingSupported
            checked: settingsBackend.watermarkApplyToRecording
            onToggled: function(checked) { settingsBackend.watermarkApplyToRecording = checked }
        }

        Spacer {}

        // Main content: controls + preview side by side
        RowLayout {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            spacing: SemanticTokens.spacing16

            // Left side: controls
            Column {
                Layout.fillWidth: true
                spacing: SemanticTokens.spacing4

                SettingsPathPicker {
                    objectName: "watermarkImage"
                    label: qsTr("Image")
                    path: settingsBackend.watermarkImagePath
                    onBrowseClicked: settingsBackend.browseWatermarkImage()
                }

                SettingsSlider {
                    resetKey: "watermarkScale"
                    label: qsTr("Scale")
                    value: settingsBackend.watermarkScale
                    from: 10
                    to: 200
                    suffix: "%"
                    onMoved: function(value) { settingsBackend.watermarkScale = value }
                }

                SettingsSlider {
                    resetKey: "watermarkOpacity"
                    label: qsTr("Opacity")
                    value: settingsBackend.watermarkOpacity
                    from: 10
                    to: 100
                    suffix: "%"
                    onMoved: function(value) { settingsBackend.watermarkOpacity = value }
                }

                SettingsSlider {
                    resetKey: "watermarkMargin"
                    label: qsTr("Margin")
                    value: settingsBackend.watermarkMargin
                    from: 0
                    to: 100
                    suffix: " px"
                    onMoved: function(value) { settingsBackend.watermarkMargin = value }
                }

                SettingsCombo {
                    resetKey: "watermarkPosition"
                    label: qsTr("Position")
                    model: [
                        { text: qsTr("Top-Left"), value: 0 },
                        { text: qsTr("Top-Right"), value: 1 },
                        { text: qsTr("Bottom-Left"), value: 2 },
                        { text: qsTr("Bottom-Right"), value: 3 }
                    ]
                    currentIndex: settingsBackend.watermarkPosition
                    onActivated: function(index) { settingsBackend.watermarkPosition = index }
                }
            }

            // Right side: preview
            Column {
                id: previewColumn
                Layout.preferredWidth: ComponentTokens.watermarkPreviewColumnWidth
                spacing: SemanticTokens.spacing4

                Rectangle {
                    width: ComponentTokens.watermarkPreviewSize
                    height: ComponentTokens.watermarkPreviewSize
                    radius: SemanticTokens.radiusSmall
                    color: SemanticTokens.backgroundElevated
                    border.width: 1
                    border.color: SemanticTokens.borderDefault

                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        source: settingsBackend.watermarkPreviewUrl
                        fillMode: Image.PreserveAspectFit
                        visible: source != ""
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("No image")
                        color: SemanticTokens.textTertiary
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.family: SemanticTokens.fontFamily
                        font.letterSpacing: SemanticTokens.letterSpacingDefault
                        visible: settingsBackend.watermarkPreviewUrl === ""
                    }
                }
            }
        }

        SettingsSection { title: qsTr("Library") }

        SettingsSlider {
            resetKey: "pinMaxCacheFiles"
            objectName: "libraryMaxScreenshotsSlider"
            label: qsTr("Screenshots kept in Library")
            value: settingsBackend.pinMaxCacheFiles
            from: 5
            to: 200
            onMoved: function(value) { settingsBackend.pinMaxCacheFiles = value }
        }

        Text {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            text: qsTr("When there are more, the oldest are removed from the Library. Files you saved stay where they are.")
            color: SemanticTokens.textTertiary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            wrapMode: Text.WordWrap
        }
    }
}
