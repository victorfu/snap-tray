import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SnapTrayQml

/**
 * WatermarkSettings: Watermark image, scale, opacity, margin, position + preview.
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

        SettingsToggle {
            label: qsTr("Apply to images")
            checked: settingsBackend.watermarkEnabled
            onToggled: settingsBackend.watermarkEnabled = checked
        }

        SettingsToggle {
            label: qsTr("Apply to recordings")
            checked: settingsBackend.watermarkApplyToRecording
            onToggled: settingsBackend.watermarkApplyToRecording = checked
        }

        Spacer {}

        // Main content: controls + preview side by side
        RowLayout {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            spacing: PrimitiveTokens.spacing16

            // Left side: controls
            Column {
                Layout.fillWidth: true
                spacing: PrimitiveTokens.spacing4

                SettingsPathPicker {
                    label: qsTr("Image")
                    path: settingsBackend.watermarkImagePath
                    onBrowseClicked: settingsBackend.browseWatermarkImage()
                }

                SettingsSlider {
                    label: qsTr("Scale")
                    value: settingsBackend.watermarkScale
                    from: 10
                    to: 200
                    suffix: "%"
                    onMoved: settingsBackend.watermarkScale = value
                }

                SettingsSlider {
                    label: qsTr("Opacity")
                    value: settingsBackend.watermarkOpacity
                    from: 10
                    to: 100
                    suffix: "%"
                    onMoved: settingsBackend.watermarkOpacity = value
                }

                SettingsSlider {
                    label: qsTr("Margin")
                    value: settingsBackend.watermarkMargin
                    from: 0
                    to: 100
                    suffix: " px"
                    onMoved: settingsBackend.watermarkMargin = value
                }

                SettingsCombo {
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
                spacing: PrimitiveTokens.spacing4

                Rectangle {
                    width: ComponentTokens.watermarkPreviewSize
                    height: ComponentTokens.watermarkPreviewSize
                    radius: PrimitiveTokens.radiusSmall
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
                        font.pixelSize: PrimitiveTokens.fontSizeCaption
                        font.family: PrimitiveTokens.fontFamily
                        font.letterSpacing: PrimitiveTokens.letterSpacingTight
                        visible: settingsBackend.watermarkPreviewUrl === ""
                    }
                }
            }
        }
    }
}
