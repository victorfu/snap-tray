pragma ComponentBehavior: Bound

import QtQuick
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null
    readonly property int compactDialogWidth: 420

    title: {
        if (!viewModel) return ""
        return viewModel.mode === 1
            ? qsTr("Update Checks Unavailable")
            : qsTr("Unable to check for updates")
    }
    subtitle: ""
    iconText: {
        if (!viewModel) return ""
        return viewModel.mode === 1 ? "\u2139" : "\u26A0"
    }
    dialogWidth: compactDialogWidth

    onCloseRequested: if (viewModel) viewModel.close()

    contentItem: Component {
        Item {
            implicitHeight: contentLoader.implicitHeight

            Loader {
                id: contentLoader
                anchors.fill: parent
                sourceComponent: root.viewModel ? messageContent : emptyContent
            }

            Component {
                id: emptyContent
                Item { implicitHeight: 20 }
            }

            Component {
                id: messageContent
                Item {
                    implicitHeight: messageColumn.implicitHeight + 28

                    Column {
                        id: messageColumn
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        Text {
                            width: parent.width
                            text: (root.viewModel && root.viewModel.errorMessage.length > 0)
                                ? root.viewModel.errorMessage
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
                    if (!root.viewModel) return emptyButtons
                    return root.viewModel.mode === 1 ? okButton : errorButtons
                }
            }

            Component {
                id: emptyButtons
                Item {}
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
                        onClicked: if (root.viewModel) root.viewModel.close()
                    }

                    DialogButton {
                        text: qsTr("Try Again")
                        style: "primary"
                        width: (parent.width - 8) / 2
                        onClicked: if (root.viewModel) root.viewModel.retry()
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
                        onClicked: if (root.viewModel) root.viewModel.close()
                    }
                }
            }
        }
    }
}
