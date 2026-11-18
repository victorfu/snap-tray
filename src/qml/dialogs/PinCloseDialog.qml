pragma ComponentBehavior: Bound
import QtQuick
import SnapTrayQml

DialogBase {
    id: root
    required property var viewModel
    objectName: "pinCloseDialog"
    title: qsTranslate("PinWindow", "Unsaved pin edits")
    iconText: "?"
    Accessible.role: Accessible.AlertMessage
    Accessible.name: title
    onCloseRequested: viewModel.cancel()

    contentItem: Component {
        Text {
            padding: 16
            text: qsTranslate("PinWindow", "Save your edits before closing this pin?")
            font.family: SemanticTokens.fontFamily
            color: SemanticTokens.textPrimary
            wrapMode: Text.Wrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
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
                    objectName: "discardEdits"
                    text: qsTranslate("PinWindow", "Discard")
                    onClicked: root.viewModel.discard()
                }
                DialogButton {
                    objectName: "cancelClose"
                    text: qsTranslate("PinWindow", "Cancel")
                    Component.onCompleted: forceActiveFocus()
                    onClicked: root.viewModel.cancel()
                }
                DialogButton {
                    objectName: "saveEdits"
                    text: qsTranslate("PinWindow", "Save")
                    style: "primary"
                    onClicked: root.viewModel.save()
                }
            }
        }
    }
}
