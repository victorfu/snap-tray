pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root

    property var viewModel: null

    title: qsTr("Recover Recordings")
    subtitle: qsTr("SnapTray stopped before these recordings were saved.")
    iconText: "↺"
    dialogWidth: 520

    // The title and button bars, and room kept around the dialog: with many
    // recordings the list scrolls instead of growing past the screen, so
    // every row and Later stay reachable (#172 review).
    readonly property int dialogChromeHeight: 72 + 56
    readonly property int screenMargin: 80
    readonly property int minimumListHeight: 160

    onCloseRequested: if (viewModel) viewModel.later()

    contentItem: Component {
        Item {
            readonly property real maximumHeight: Math.max(root.minimumListHeight,
                Screen.desktopAvailableHeight - root.dialogChromeHeight - root.screenMargin)
            implicitHeight: Math.min(list.implicitHeight + 28, maximumHeight)

            Flickable {
                id: scroller
                objectName: "recoveryList"
                anchors.fill: parent
                anchors.margins: 14
                contentWidth: width
                contentHeight: list.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: list
                    width: scroller.width
                    spacing: 10

                    Repeater {
                        model: root.viewModel ? root.viewModel.recordings : []

                        Rectangle {
                            required property var modelData
                            required property int index
                            width: list.width
                            height: row.implicitHeight + 16
                            radius: SemanticTokens.radiusSmall
                            color: ComponentTokens.inputBackground
                            border.width: 1
                            border.color: ComponentTokens.inputBorder

                            Row {
                                id: row
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 8

                                Column {
                                    width: parent.width - buttons.width - parent.spacing
                                    spacing: 2
                                    Text {
                                        width: parent.width
                                        elide: Text.ElideRight
                                        text: modelData.title
                                        font.pixelSize: SemanticTokens.fontSizeBody
                                        font.family: SemanticTokens.fontFamily
                                        color: SemanticTokens.textPrimary
                                    }
                                    Text {
                                        width: parent.width
                                        wrapMode: Text.Wrap
                                        text: modelData.status
                                        font.pixelSize: SemanticTokens.fontSizeCaption
                                        font.family: SemanticTokens.fontFamily
                                        color: SemanticTokens.textSecondary
                                    }
                                }

                                Row {
                                    id: buttons
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 6
                                    DialogButton {
                                        objectName: "recoveryRecoverButton"
                                        visible: modelData.canRecover
                                        height: 30
                                        text: qsTr("Recover")
                                        style: "primary"
                                        Accessible.role: Accessible.Button
                                        Accessible.name: qsTr("Recover %1").arg(modelData.title)
                                        onClicked: root.viewModel.recover(index)
                                    }
                                    DialogButton {
                                        objectName: "recoveryDiscardButton"
                                        height: 30
                                        text: qsTr("Discard")
                                        style: "secondary"
                                        Accessible.role: Accessible.Button
                                        Accessible.name: qsTr("Discard %1").arg(modelData.title)
                                        onClicked: root.viewModel.discard(index)
                                    }
                                }
                            }
                        }
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
                anchors.margins: 8
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                layoutDirection: Qt.RightToLeft
                DialogButton {
                    objectName: "recoveryLaterButton"
                    text: qsTr("Later")
                    style: "secondary"
                    width: 100
                    onClicked: root.viewModel.later()
                }
            }
        }
    }
}
