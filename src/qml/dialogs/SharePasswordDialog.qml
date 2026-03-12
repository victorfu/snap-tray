import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null

    title: qsTr("Share URL")
    subtitle: qsTr("Set an optional password")
    iconText: "URL"
    dialogWidth: 420

    onCloseRequested: if (viewModel) viewModel.cancel()

    contentItem: Component {
        Item {
            implicitHeight: contentCol.implicitHeight + 28

            Column {
                id: contentCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                TextField {
                    id: passwordField
                    width: parent.width
                    height: 36
                    verticalAlignment: TextInput.AlignVCenter
                    echoMode: TextInput.Password
                    placeholderText: qsTr("No password")
                    maximumLength: viewModel ? viewModel.maxLength : 32
                    text: viewModel ? viewModel.password : ""
                    onTextChanged: if (viewModel) viewModel.password = text

                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    color: SemanticTokens.textPrimary
                    selectionColor: ComponentTokens.inputSelectionBackground
                    selectedTextColor: ComponentTokens.inputSelectionText
                    placeholderTextColor: SemanticTokens.textTertiary

                    background: Rectangle {
                        radius: DesignSystem.inputRadius
                        color: DesignSystem.inputBackground
                        border.width: passwordField.activeFocus ? 2 : 1
                        border.color: passwordField.activeFocus
                            ? DesignSystem.inputBorderFocus
                            : DesignSystem.inputBorder
                    }

                    Keys.onReturnPressed: if (viewModel) viewModel.accept()
                    Keys.onEnterPressed: if (viewModel) viewModel.accept()

                    Component.onCompleted: forceActiveFocus()
                }

                Text {
                    width: parent.width
                    text: qsTr("Leave empty for public access. Link expires in 24 hours.")
                    color: SemanticTokens.textSecondary
                    font.pixelSize: DesignSystem.fontSizeSmallBody
                    font.family: SemanticTokens.fontFamily
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    buttonBar: Component {
        Item {
            implicitHeight: 56

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12; anchors.rightMargin: 12
                anchors.topMargin: 8; anchors.bottomMargin: 8
                spacing: 8

                DialogButton {
                    text: qsTr("Cancel")
                    style: "secondary"
                    width: (parent.width - 8) / 2
                    onClicked: if (viewModel) viewModel.cancel()
                }

                DialogButton {
                    text: qsTr("Share")
                    style: "primary"
                    width: (parent.width - 8) / 2
                    onClicked: if (viewModel) viewModel.accept()
                }
            }
        }
    }
}
