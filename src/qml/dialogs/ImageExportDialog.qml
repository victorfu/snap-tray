pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * ImageExportDialog: size, format and JPEG quality of an exported still
 * image, with the real size of the file that will be written (#357).
 */
DialogBase {
    id: root
    property var viewModel: null
    readonly property int contentMargin: 16
    readonly property int resizeOriginal: 0
    readonly property int resizePercent: 1
    readonly property int resizeLongEdge: 2
    readonly property int longEdgeStep: 16
    readonly property int formatPng: 0
    readonly property int formatJpeg: 1

    title: qsTr("Export Image")
    subtitle: qsTr("Save a smaller copy. The original stays as it is.")
    iconText: "⤓"
    dialogWidth: 460
    onCloseRequested: if (viewModel) viewModel.cancel()

    contentItem: Component {
        Column {
            width: root.width
            padding: root.contentMargin
            spacing: 8

            SettingsCombo {
                objectName: "exportResizeMode"
                width: parent.width - 2 * root.contentMargin
                label: qsTr("Size")
                model: [
                    { text: qsTr("Original size"), value: root.resizeOriginal },
                    { text: qsTr("Scale by percent"), value: root.resizePercent },
                    { text: qsTr("Limit long edge"), value: root.resizeLongEdge }
                ]
                currentIndex: root.viewModel ? root.viewModel.resizeMode : 0
                onActivated: function(index) { root.viewModel.resizeMode = index }
            }
            SettingsSlider {
                objectName: "exportPercent"
                width: parent.width - 2 * root.contentMargin
                visible: root.viewModel && root.viewModel.resizeMode === root.resizePercent
                label: qsTr("Scale")
                suffix: "%"
                from: root.viewModel ? root.viewModel.minPercent : 1
                to: root.viewModel ? root.viewModel.maxPercent : 100
                value: root.viewModel ? root.viewModel.percent : 100
                onMoved: function(value) { root.viewModel.percent = value }
            }
            SettingsSlider {
                objectName: "exportLongEdge"
                width: parent.width - 2 * root.contentMargin
                visible: root.viewModel && root.viewModel.resizeMode === root.resizeLongEdge
                label: qsTr("Long edge")
                suffix: " px"
                from: root.viewModel ? root.viewModel.minLongEdge : 16
                to: root.viewModel ? root.viewModel.maxLongEdge : 16
                stepSize: root.longEdgeStep
                value: root.viewModel ? root.viewModel.longEdge : 16
                onMoved: function(value) { root.viewModel.longEdge = value }
            }
            SettingsCombo {
                objectName: "exportFormat"
                width: parent.width - 2 * root.contentMargin
                label: qsTr("Format")
                model: [
                    { text: qsTr("PNG (lossless)"), value: root.formatPng },
                    { text: qsTr("JPEG (lossy)"), value: root.formatJpeg }
                ]
                currentIndex: root.viewModel ? root.viewModel.format : 0
                onActivated: function(index) { root.viewModel.format = index }
            }
            // PNG has no lossy quality, so the control is not offered for it.
            SettingsSlider {
                objectName: "exportJpegQuality"
                width: parent.width - 2 * root.contentMargin
                visible: root.viewModel && root.viewModel.lossy
                label: qsTr("JPEG quality")
                from: root.viewModel ? root.viewModel.minJpegQuality : 1
                to: root.viewModel ? root.viewModel.maxJpegQuality : 100
                value: root.viewModel ? root.viewModel.jpegQuality : 90
                onMoved: function(value) { root.viewModel.jpegQuality = value }
            }
            Text {
                objectName: "exportSourceSize"
                width: parent.width - 2 * root.contentMargin
                text: root.viewModel ? qsTr("Original: %1").arg(root.viewModel.sourceSizeText) : ""
                color: SemanticTokens.textSecondary
                font.family: SemanticTokens.fontFamily
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                objectName: "exportOutputSize"
                width: parent.width - 2 * root.contentMargin
                text: root.viewModel ? qsTr("Output: %1").arg(root.viewModel.outputSizeText) : ""
                color: SemanticTokens.textPrimary
                font.family: SemanticTokens.fontFamily
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                objectName: "exportFileSize"
                width: parent.width - 2 * root.contentMargin
                text: root.viewModel ? qsTr("File size: %1").arg(root.viewModel.fileSizeText) : ""
                color: SemanticTokens.textPrimary
                font.family: SemanticTokens.fontFamily
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                objectName: "exportNote"
                width: parent.width - 2 * root.contentMargin
                text: root.viewModel ? root.viewModel.noteText : ""
                color: SemanticTokens.textSecondary
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.fontSizeCaption
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                objectName: "exportError"
                width: parent.width - 2 * root.contentMargin
                visible: text.length > 0
                text: root.viewModel ? root.viewModel.errorText : ""
                color: SemanticTokens.statusError
                font.family: SemanticTokens.fontFamily
                wrapMode: Text.Wrap
                Accessible.role: Accessible.AlertMessage
                Accessible.name: text
            }
        }
    }
    buttonBar: Component {
        Item {
            height: 56
            Row {
                anchors.right: parent.right
                anchors.rightMargin: root.contentMargin
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                DialogButton {
                    objectName: "exportCancel"
                    text: qsTr("Cancel")
                    style: "secondary"
                    onClicked: if (root.viewModel) root.viewModel.cancel()
                }
                DialogButton {
                    objectName: "exportSave"
                    text: qsTr("Export...")
                    enabled: root.viewModel && root.viewModel.canSave
                    onClicked: if (root.viewModel) root.viewModel.save()
                }
            }
        }
    }
}
