pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null
    title: qsTr("SnapTray Update")
    subtitle: root.viewModel && root.viewModel.version.length > 0 ? qsTr("Version %1").arg(root.viewModel.version) : ""
    iconText: "↑"
    dialogWidth: 480
    onCloseRequested: if (root.viewModel) root.viewModel.close()
    contentItem: Component {
        Item {
            implicitHeight: content.implicitHeight + 28
            Column {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 16
            Text {
                width: parent.width
                text: root.viewModel ? root.viewModel.message : ""
                textFormat: Text.PlainText
                color: SemanticTokens.textPrimary
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.fontSizeBody
                wrapMode: Text.WordWrap
            }
            ProgressBar {
                width: parent.width
                visible: root.viewModel && ["downloading", "verifying", "installing", "checking"].indexOf(root.viewModel.state) >= 0
                indeterminate: root.viewModel && root.viewModel.state !== "downloading"
                value: root.viewModel ? root.viewModel.progress : 0
            }
            DialogButton {
                visible: root.viewModel && root.viewModel.state === "error"
                width: 200
                text: qsTr("Discard Download")
                style: "secondary"
                onClicked: if (root.viewModel) root.viewModel.discard()
            }
            ScrollView {
                width: parent.width
                height: visible ? 200 : 0
                visible: root.viewModel && root.viewModel.releaseNotes.length > 0
                clip: true
                TextArea {
                    text: root.viewModel ? root.viewModel.releaseNotes : ""
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    color: SemanticTokens.textSecondary
                    font.family: SemanticTokens.fontFamily
                    font.pixelSize: SemanticTokens.fontSizeBody
                    background: null
                }
            }
        }
    }
    }
    buttonBar: Component {
        Item {
            implicitHeight: 56
            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8
            DialogButton {
                width: 130
                text: root.viewModel && root.viewModel.state === "ready" ? qsTr("Later") : qsTr("Close")
                style: "secondary"
                onClicked: if (root.viewModel) root.viewModel.close()
            }
            DialogButton {
                width: 180
                visible: root.viewModel && ["available", "ready", "error", "downloading", "idle", "current"].indexOf(root.viewModel.state) >= 0
                style: "primary"
                text: {
                    if (!root.viewModel) return ""
                    switch (root.viewModel.state) {
                    case "available": return qsTr("Download Update")
                    case "ready": return qsTr("Restart and Update")
                    case "downloading": return qsTr("Cancel Download")
                    case "idle":
                    case "current": return qsTr("Check Again")
                    default: return qsTr("Try Again")
                    }
                }
                onClicked: {
                    if (!root.viewModel) return
                    switch (root.viewModel.state) {
                    case "available": root.viewModel.download(); break
                    case "ready": root.viewModel.install(); break
                    case "downloading": root.viewModel.cancel(); break
                    default: root.viewModel.retry()
                    }
                }
            }
            DialogButton {
                width: 130
                visible: root.viewModel && root.viewModel.state === "error"
                text: qsTr("Downloads")
                style: "secondary"
                onClicked: if (root.viewModel) root.viewModel.openDownloads()
            }
        }
    }
    }
}
