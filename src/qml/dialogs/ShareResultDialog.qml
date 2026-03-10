import QtQuick
import QtQuick.Controls
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null

    title: qsTr("Share URL Ready")
    subtitle: viewModel ? viewModel.subtitleText : ""
    iconText: "URL"
    dialogWidth: 460

    onCloseRequested: if (viewModel) viewModel.close()

    contentItem: Component {
        Item {
            implicitHeight: contentCol.implicitHeight + 28

            Column {
                id: contentCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                // URL caption
                Text {
                    text: qsTr("Share Link")
                    color: SemanticTokens.textSecondary
                    font.pixelSize: DesignSystem.fontSizeSmallBody
                    font.family: SemanticTokens.fontFamily
                    font.weight: Font.Medium
                }

                // URL field (read-only)
                TextField {
                    width: parent.width
                    height: 36
                    verticalAlignment: TextInput.AlignVCenter
                    text: viewModel ? viewModel.url : ""
                    readOnly: true
                    selectByMouse: true
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "Consolas"
                    color: SemanticTokens.textPrimary
                    placeholderText: qsTr("No URL")
                    placeholderTextColor: SemanticTokens.textTertiary
                    background: Rectangle {
                        radius: DesignSystem.inputRadius
                        color: DesignSystem.inputBackground
                        border.width: 1
                        border.color: DesignSystem.inputBorder
                    }
                }

                // Password section (conditional)
                Text {
                    visible: viewModel ? viewModel.hasPassword : false
                    text: qsTr("Password")
                    color: SemanticTokens.textSecondary
                    font.pixelSize: DesignSystem.fontSizeSmallBody
                    font.family: SemanticTokens.fontFamily
                    font.weight: Font.Medium
                }

                TextField {
                    visible: viewModel ? viewModel.hasPassword : false
                    width: parent.width
                    height: 36
                    verticalAlignment: TextInput.AlignVCenter
                    text: viewModel ? viewModel.password : ""
                    readOnly: true
                    selectByMouse: true
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "Consolas"
                    color: SemanticTokens.textPrimary
                    background: Rectangle {
                        radius: DesignSystem.inputRadius
                        color: DesignSystem.inputBackground
                        border.width: 1
                        border.color: DesignSystem.inputBorder
                    }
                }

                // Badges row
                Row {
                    spacing: 8

                    Rectangle {
                        visible: viewModel ? viewModel.expiresText.length > 0 : false
                        width: expiresLabel.implicitWidth + 20
                        height: expiresLabel.implicitHeight + 8
                        radius: 10
                        color: ComponentTokens.badgeBackground
                        border.width: 1
                        border.color: ComponentTokens.badgeBorder

                        Text {
                            id: expiresLabel
                            anchors.centerIn: parent
                            text: viewModel ? viewModel.expiresText : ""
                            color: SemanticTokens.textSecondary
                            font.pixelSize: DesignSystem.fontSizeSmallBody
                            font.family: SemanticTokens.fontFamily
                        }
                    }

                    Rectangle {
                        width: protectedLabel.implicitWidth + 20
                        height: protectedLabel.implicitHeight + 8
                        radius: 10
                        color: ComponentTokens.badgeBackground
                        border.width: 1
                        border.color: ComponentTokens.badgeBorder

                        Text {
                            id: protectedLabel
                            anchors.centerIn: parent
                            text: viewModel ? viewModel.protectedText : ""
                            color: SemanticTokens.textSecondary
                            font.pixelSize: DesignSystem.fontSizeSmallBody
                            font.family: SemanticTokens.fontFamily
                        }
                    }
                }

                // Hint text
                Text {
                    width: parent.width
                    text: viewModel ? viewModel.hintText : ""
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
                    text: qsTr("Close")
                    style: "secondary"
                    width: (parent.width - 16) / 3
                    onClicked: if (viewModel) viewModel.close()
                }

                DialogButton {
                    id: copyBtn
                    text: qsTr("Copy")
                    style: "secondary"
                    feedbackText: qsTr("\u2713 Copied!")
                    width: (parent.width - 16) / 3
                    onClicked: {
                        if (viewModel) viewModel.copyToClipboard()
                        copyBtn.showFeedback()
                    }
                }

                DialogButton {
                    text: qsTr("Open")
                    style: "primary"
                    width: (parent.width - 16) / 3
                    onClicked: if (viewModel) viewModel.openInBrowser()
                }
            }
        }
    }
}
