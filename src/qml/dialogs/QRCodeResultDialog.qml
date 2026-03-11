import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null

    title: viewModel ? viewModel.formatDisplayName : ""
    subtitle: viewModel ? viewModel.characterCountText : ""
    iconText: "QR"
    dialogWidth: 420

    onCloseRequested: if (viewModel) viewModel.close()

    contentItem: Component {
        Item {
            implicitHeight: contentCol.implicitHeight + 28

            Column {
                id: contentCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                // Text edit area
                ScrollView {
                    width: parent.width
                    height: Math.min(Math.max(100, textArea.contentHeight + 20), 200)

                    TextArea {
                        id: textArea
                        text: viewModel ? viewModel.text : ""
                        onTextChanged: if (viewModel) viewModel.text = text
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        color: SemanticTokens.textPrimary
                        placeholderText: qsTr("No content")
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

                        Component.onCompleted: forceActiveFocus()
                    }
                }

                // Generated QR preview
                Rectangle {
                    width: parent.width
                    height: 176
                    radius: DesignSystem.inputRadius
                    color: DesignSystem.inputBackground
                    border.width: 1
                    border.color: DesignSystem.inputBorder

                    Image {
                        anchors.centerIn: parent
                        width: 160; height: 160
                        fillMode: Image.PreserveAspectFit
                        source: viewModel ? viewModel.generatedPreviewSource : ""
                        visible: viewModel ? viewModel.hasGeneratedImage : false
                        cache: false
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !(viewModel ? viewModel.hasGeneratedImage : false)
                        text: (viewModel && viewModel.canGenerate)
                            ? qsTr("Generate QR to preview")
                            : ((viewModel && viewModel.text.length > 0)
                                ? qsTr("Content too long")
                                : qsTr("Generate QR to preview"))
                        color: SemanticTokens.textTertiary
                        font.pixelSize: DesignSystem.fontSizeSmallBody
                        font.family: SemanticTokens.fontFamily
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
                spacing: 6

                DialogButton {
                    text: qsTr("Close")
                    style: "secondary"
                    width: btnWidth()
                    onClicked: if (viewModel) viewModel.close()
                }

                DialogButton {
                    id: copyBtn
                    text: qsTr("Copy")
                    style: "secondary"
                    feedbackText: qsTr("\u2713 Copied!")
                    width: btnWidth()
                    onClicked: {
                        if (viewModel) viewModel.copyText()
                        copyBtn.showFeedback()
                    }
                }

                DialogButton {
                    visible: viewModel ? viewModel.hasUrl : false
                    text: qsTr("Open URL")
                    style: "secondary"
                    width: (viewModel && viewModel.hasUrl) ? btnWidth() : 0
                    onClicked: if (viewModel) viewModel.openUrl()
                }

                DialogButton {
                    text: qsTr("Generate")
                    style: "primary"
                    enabled: viewModel ? viewModel.canGenerate : false
                    width: btnWidth()
                    onClicked: if (viewModel) viewModel.generateQR()
                }

                DialogButton {
                    visible: viewModel ? viewModel.pinActionAvailable : false
                    text: qsTr("Pin QR")
                    style: "secondary"
                    enabled: viewModel ? viewModel.hasGeneratedImage : false
                    width: (viewModel && viewModel.pinActionAvailable) ? btnWidth() : 0
                    onClicked: if (viewModel) viewModel.pinQR()
                }
            }

            function btnWidth() {
                let count = 3  // Close, Copy, Generate always visible
                if (viewModel && viewModel.hasUrl) count++
                if (viewModel && viewModel.pinActionAvailable) count++
                return (parent.width - 24 - (count - 1) * 6) / count
            }
        }
    }
}
