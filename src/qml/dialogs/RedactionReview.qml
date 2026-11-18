pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null
    iconText: "▦"
    title: qsTr("Review Auto Blur")
    readonly property int thumbnailWidth: 275
    readonly property int thumbnailHeight: 60
    dialogWidth: 440
    onCloseRequested: if (viewModel) viewModel.cancel()

    contentItem: Component {
        Column {
            width: root.width
            Text {
                width: parent.width
                leftPadding: 12
                rightPadding: 12
                bottomPadding: 8
                wrapMode: Text.Wrap
                color: SemanticTokens.textSecondary
                font.family: SemanticTokens.fontFamily
                text: qsTr("Select the regions to hide. OCR can miss sensitive text.")
            }
            Text {
                width: parent.width
                leftPadding: 12
                rightPadding: 12
                bottomPadding: 8
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: SemanticTokens.textPrimary
                text: root.viewModel ? root.viewModel.warning : ""
                visible: text.length > 0
            }
            ListView {
                id: list
                width: root.width
                height: Math.min(320, contentHeight)
                clip: true
                model: root.viewModel ? root.viewModel.thumbnails : []
                ScrollBar.vertical: ScrollBar {}
                delegate: Row {
                    id: row
                    required property string modelData
                    required property int index
                    height: root.thumbnailHeight + 8
                    CheckBox {
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property point origin: root.viewModel.origin(row.index)
                        text: qsTr("Region %1\n(%2, %3)").arg(row.index + 1).arg(origin.x).arg(origin.y)
                        checked: root.viewModel.isSelected(row.index)
                        onToggled: root.viewModel.select(row.index, checked)
                    }
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.thumbnailWidth
                        height: root.thumbnailHeight
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        source: row.modelData
                    }
                }
            }
        }
    }
    buttonBar: Component {
        Item {
            height: 56
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                DialogButton {
                    objectName: "cancelReview"
                    text: qsTr("Cancel")
                    style: "secondary"
                    onClicked: root.viewModel.cancel()
                }
                DialogButton {
                    text: qsTr("Apply")
                    onClicked: root.viewModel.apply()
                }
            }
        }
    }
}
