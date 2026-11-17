pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null
    title: qsTr("Arrange Images")
    subtitle: qsTr("Preview the layout before applying")
    iconText: "▦"
    dialogWidth: 460
    onCloseRequested: if (viewModel) viewModel.cancelRequested()

    contentItem: Component {
        Column {
            width: root.width
            padding: 16
            spacing: 8
            SettingsCombo {
                objectName: "layoutMode"
                width: parent.width - 32
                label: qsTr("Layout")
                model: [
                    { text: qsTr("Manual"), value: 0 },
                    { text: qsTr("Original sizes"), value: 1 },
                    { text: qsTr("Horizontal, equal height"), value: 2 },
                    { text: qsTr("Vertical, equal width"), value: 3 }
                ]
                currentIndex: root.viewModel ? root.viewModel.arrangementMode : 0
                onActivated: function(index) { root.viewModel.setArrangementMode(index) }
            }
            SettingsToggle {
                width: parent.width - 32
                label: qsTr("Do not enlarge source pixels")
                description: qsTr("Use the smallest source as the size limit")
                enabled: root.viewModel && root.viewModel.arrangementMode !== 0
                checked: root.viewModel ? root.viewModel.preventUpscale : true
                onToggled: function(checked) { root.viewModel.setPreventUpscale(checked) }
            }
            SettingsSlider {
                objectName: "layoutGap"
                width: parent.width - 32
                label: qsTr("Spacing")
                from: 0
                to: 200
                value: root.viewModel ? root.viewModel.arrangementGap : 8
                enabled: root.viewModel && root.viewModel.arrangementMode !== 0
                onMoved: function(value) { root.viewModel.setArrangementGap(value) }
            }
            Text {
                width: parent.width - 32
                text: qsTr("Image order (numbers match the canvas)")
                color: SemanticTokens.textPrimary
                font.family: SemanticTokens.fontFamily
                wrapMode: Text.Wrap
            }
            ScrollView {
                width: parent.width - 32
                height: Math.min(160, orderColumn.implicitHeight)
                clip: true
                Column {
                    id: orderColumn
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: root.viewModel ? root.viewModel.arrangementOrder : []
                        Row {
                            id: imageRow
                            required property int modelData
                            required property int index
                            spacing: 8
                            Text {
                                width: 130
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Image %1").arg(imageRow.modelData + 1)
                                color: SemanticTokens.textPrimary
                                font.family: SemanticTokens.fontFamily
                            }
                            DialogButton {
                                text: qsTr("Earlier")
                                style: "secondary"
                                enabled: imageRow.index > 0 && root.viewModel.arrangementMode !== 0
                                Accessible.name: qsTr("Move image %1 earlier").arg(imageRow.modelData + 1)
                                onClicked: root.viewModel.moveArrangementItem(imageRow.index, -1)
                            }
                            DialogButton {
                                text: qsTr("Later")
                                style: "secondary"
                                enabled: root.viewModel && imageRow.index + 1 < root.viewModel.arrangementOrder.length
                                    && root.viewModel.arrangementMode !== 0
                                Accessible.name: qsTr("Move image %1 later").arg(imageRow.modelData + 1)
                                onClicked: root.viewModel.moveArrangementItem(imageRow.index, 1)
                            }
                        }
                    }
                }
            }
            Text {
                objectName: "layoutOutputSize"
                width: parent.width - 32
                text: root.viewModel ? root.viewModel.outputDescription : ""
                color: SemanticTokens.textPrimary
                font.family: SemanticTokens.fontFamily
                wrapMode: Text.Wrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            Text {
                width: parent.width - 32
                text: qsTr("Choose Manual to restore your free layout and move images on the canvas.")
                color: SemanticTokens.textSecondary
                font.family: SemanticTokens.fontFamily
                wrapMode: Text.Wrap
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
                    text: qsTr("Cancel")
                    style: "secondary"
                    onClicked: if (root.viewModel) root.viewModel.cancelRequested()
                }
                DialogButton {
                    objectName: "layoutApply"
                    text: qsTr("Apply")
                    enabled: root.viewModel && root.viewModel.arrangementValid
                    onClicked: if (root.viewModel) root.viewModel.confirmRequested()
                }
            }
        }
    }
}
