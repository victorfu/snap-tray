import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null

    title: qsTr("OCR Result")
    subtitle: viewModel ? viewModel.characterCountText : ""
    iconText: "OCR"
    dialogWidth: 420

    onCloseRequested: if (viewModel) viewModel.close()

    contentItem: Component {
        Item {
            implicitHeight: Math.min(Math.max(180, textArea.contentHeight + 28), 340)

            ScrollView {
                anchors.fill: parent
                anchors.margins: 14

                TextArea {
                    id: textArea
                    text: viewModel ? viewModel.text : ""
                    onTextChanged: if (viewModel) viewModel.text = text
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    color: SemanticTokens.textPrimary
                    placeholderText: qsTr("No text recognized")
                    placeholderTextColor: SemanticTokens.textTertiary
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "Consolas"

                    background: Rectangle {
                        radius: DesignSystem.inputRadius
                        color: DesignSystem.inputBackground
                        border.width: textArea.activeFocus ? 2 : 1
                        border.color: textArea.activeFocus
                            ? DesignSystem.inputBorderFocus
                            : DesignSystem.inputBorder
                    }

                    Component.onCompleted: {
                        selectAll()
                        forceActiveFocus()
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
                anchors.leftMargin: 12; anchors.rightMargin: 12
                anchors.topMargin: 8; anchors.bottomMargin: 8
                spacing: 8

                DialogButton {
                    text: qsTr("Close")
                    style: "secondary"
                    width: (viewModel && viewModel.hasTsv)
                        ? (parent.width - 16) / 3
                        : (parent.width - 8) / 2
                    onClicked: if (viewModel) viewModel.close()
                }

                DialogButton {
                    id: copyBtn
                    text: qsTr("Copy")
                    style: "secondary"
                    feedbackText: qsTr("\u2713 Copied!")
                    width: (viewModel && viewModel.hasTsv)
                        ? (parent.width - 16) / 3
                        : (parent.width - 8) / 2
                    onClicked: {
                        if (viewModel) viewModel.copyText()
                        copyBtn.showFeedback()
                    }
                }

                DialogButton {
                    id: tsvBtn
                    visible: viewModel ? viewModel.hasTsv : false
                    text: qsTr("Copy as TSV")
                    style: "secondary"
                    feedbackText: qsTr("\u2713 Copied!")
                    width: (viewModel && viewModel.hasTsv) ? (parent.width - 16) / 3 : 0
                    onClicked: {
                        if (viewModel) viewModel.copyAsTsv()
                        tsvBtn.showFeedback()
                    }
                }
            }
        }
    }
}
