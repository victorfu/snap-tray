import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null

    title: {
        if (!viewModel) return ""
        if (viewModel.mode === 0) return qsTr("New Version Available!")
        if (viewModel.mode === 1) return qsTr("You're up to date!")
        return qsTr("Unable to check for updates")
    }
    subtitle: {
        if (!viewModel) return ""
        if (viewModel.mode === 0)
            return qsTr("%1 %2 is now available. You are using %3")
                .arg(viewModel.appName).arg(viewModel.version).arg(viewModel.currentVersion)
        if (viewModel.mode === 1)
            return qsTr("%1 %2 is the latest version.")
                .arg(viewModel.appName).arg(viewModel.currentVersion)
        return ""
    }
    iconText: {
        if (!viewModel) return ""
        if (viewModel.mode === 0) return "\uD83C\uDF89"  // party
        if (viewModel.mode === 1) return "\u2713"          // check
        return "\u26A0"                                     // warning
    }
    dialogWidth: (viewModel && viewModel.mode === 0) ? 480 : 320

    onCloseRequested: if (viewModel) viewModel.close()

    contentItem: Component {
        Item {
            implicitHeight: contentLoader.implicitHeight

            Loader {
                id: contentLoader
                anchors.fill: parent
                sourceComponent: {
                    if (!viewModel) return emptyContent
                    if (viewModel.mode === 0) return updateAvailableContent
                    if (viewModel.mode === 1) return upToDateContent
                    return errorContent
                }
            }

            Component {
                id: emptyContent
                Item { implicitHeight: 20 }
            }

            Component {
                id: updateAvailableContent
                Item {
                    implicitHeight: notesCol.implicitHeight + 28

                    Column {
                        id: notesCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        Text {
                            text: qsTr("What's New")
                            color: SemanticTokens.textPrimary
                            font.pixelSize: DesignSystem.fontSizeH3
                            font.family: SemanticTokens.fontFamily
                            font.weight: Font.DemiBold
                        }

                        ScrollView {
                            width: parent.width
                            height: Math.min(200, notesText.implicitHeight + 24)

                            TextArea {
                                id: notesText
                                text: viewModel ? viewModel.releaseNotes : ""
                                textFormat: TextEdit.MarkdownText
                                readOnly: true
                                wrapMode: TextEdit.Wrap
                                color: SemanticTokens.textPrimary
                                selectionColor: ComponentTokens.inputSelectionBackground
                                selectedTextColor: ComponentTokens.inputSelectionText
                                font.pixelSize: SemanticTokens.fontSizeBody
                                font.family: SemanticTokens.fontFamily

                                background: Rectangle {
                                    radius: DesignSystem.radiusMedium
                                    color: DesignSystem.inputBackground
                                    border.width: 1
                                    border.color: DesignSystem.inputBorder
                                }
                            }
                        }
                    }
                }
            }

            Component {
                id: upToDateContent
                Item {
                    implicitHeight: 20
                }
            }

            Component {
                id: errorContent
                Item {
                    implicitHeight: errorCol.implicitHeight + 28

                    Column {
                        id: errorCol
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        Text {
                            width: parent.width
                            text: (viewModel && viewModel.errorMessage.length > 0)
                                ? viewModel.errorMessage
                                : qsTr("Please check your internet connection and try again.")
                            color: SemanticTokens.textSecondary
                            font.pixelSize: SemanticTokens.fontSizeBody
                            font.family: SemanticTokens.fontFamily
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }
    }

    buttonBar: Component {
        Item {
            implicitHeight: 56

            Loader {
                anchors.fill: parent
                sourceComponent: {
                    if (!viewModel) return emptyButtons
                    if (viewModel.mode === 0) return updateButtons
                    if (viewModel.mode === 1) return okButton
                    return errorButtons
                }
            }

            Component {
                id: emptyButtons
                Item {}
            }

            Component {
                id: updateButtons
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12; anchors.rightMargin: 12
                    anchors.topMargin: 8; anchors.bottomMargin: 8
                    spacing: 8

                    DialogButton {
                        text: qsTr("Skip Version")
                        style: "ghost"
                        width: (parent.width - 16) / 3
                        onClicked: if (viewModel) viewModel.skipVersion()
                    }

                    DialogButton {
                        text: qsTr("Remind Later")
                        style: "secondary"
                        width: (parent.width - 16) / 3
                        onClicked: if (viewModel) viewModel.remindLater()
                    }

                    DialogButton {
                        text: qsTr("Download")
                        style: "primary"
                        width: (parent.width - 16) / 3
                        onClicked: if (viewModel) viewModel.download()
                    }
                }
            }

            Component {
                id: okButton
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12; anchors.rightMargin: 12
                    anchors.topMargin: 8; anchors.bottomMargin: 8
                    spacing: 8
                    layoutDirection: Qt.RightToLeft

                    DialogButton {
                        text: qsTr("OK")
                        style: "primary"
                        width: 100
                        onClicked: if (viewModel) viewModel.close()
                    }
                }
            }

            Component {
                id: errorButtons
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12; anchors.rightMargin: 12
                    anchors.topMargin: 8; anchors.bottomMargin: 8
                    spacing: 8

                    DialogButton {
                        text: qsTr("Close")
                        style: "secondary"
                        width: (parent.width - 8) / 2
                        onClicked: if (viewModel) viewModel.close()
                    }

                    DialogButton {
                        text: qsTr("Try Again")
                        style: "primary"
                        width: (parent.width - 8) / 2
                        onClicked: if (viewModel) viewModel.retry()
                    }
                }
            }
        }
    }
}
