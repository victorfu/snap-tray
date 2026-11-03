pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root

    property var viewModel: null
    // The row waiting for its delete to be confirmed, or -1.
    property int confirmingIndex: -1

    title: qsTr("Recording Library")
    subtitle: root.viewModel && root.viewModel.projects.length > 0
        ? qsTr("Editable projects: %1 on this computer").arg(root.viewModel.totalSizeText)
        : qsTr("No editable projects")
    iconText: "▤"
    dialogWidth: 560

    readonly property int dialogChromeHeight: 72 + 56
    readonly property int screenMargin: 80
    readonly property int minimumListHeight: 160
    readonly property int thumbnailWidth: 96
    readonly property int thumbnailHeight: 54

    onCloseRequested: if (viewModel) viewModel.close()

    contentItem: Component {
        Item {
            readonly property real maximumHeight: Math.max(root.minimumListHeight,
                Screen.desktopAvailableHeight - root.dialogChromeHeight - root.screenMargin)
            implicitHeight: Math.min(list.implicitHeight + 28, maximumHeight)

            Flickable {
                id: scroller
                objectName: "libraryList"
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

                    // Projects keep the full, unmasked recording.
                    Text {
                        objectName: "libraryPrivacyNote"
                        width: list.width
                        wrapMode: Text.Wrap
                        text: root.viewModel && !root.viewModel.keepProjects
                            ? qsTr("Keeping new projects is off. Projects keep the full recording, including anything you blurred or cropped out.")
                            : qsTr("Projects keep the full recording, including anything you blurred or cropped out.")
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.family: SemanticTokens.fontFamily
                        color: SemanticTokens.textSecondary
                    }

                    Text {
                        objectName: "libraryEmpty"
                        visible: !root.viewModel || root.viewModel.projects.length === 0
                        width: list.width
                        wrapMode: Text.Wrap
                        text: qsTr("Turn on Keep editable projects in Settings > Recording to edit recordings again after saving.")
                        font.pixelSize: SemanticTokens.fontSizeBody
                        font.family: SemanticTokens.fontFamily
                        color: SemanticTokens.textPrimary
                    }

                    Repeater {
                        model: root.viewModel ? root.viewModel.projects : []

                        Rectangle {
                            id: projectRow
                            required property var modelData
                            required property int index
                            readonly property bool confirming: root.confirmingIndex === index
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

                                Rectangle {
                                    width: root.thumbnailWidth
                                    height: root.thumbnailHeight
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: SemanticTokens.radiusSmall
                                    color: ComponentTokens.inputBorder
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: projectRow.modelData.thumbnail
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: false
                                    }
                                }

                                Column {
                                    width: parent.width - root.thumbnailWidth - buttons.width - 2 * parent.spacing
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Text {
                                        width: parent.width
                                        elide: Text.ElideMiddle
                                        text: projectRow.modelData.title
                                        font.pixelSize: SemanticTokens.fontSizeBody
                                        font.family: SemanticTokens.fontFamily
                                        color: SemanticTokens.textPrimary
                                    }
                                    Text {
                                        width: parent.width
                                        wrapMode: Text.Wrap
                                        text: projectRow.confirming
                                            ? qsTr("Delete this project? The file you saved from it stays.")
                                            : projectRow.modelData.details
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
                                        objectName: "libraryOpenButton"
                                        visible: !projectRow.confirming && !projectRow.modelData.open
                                        height: 30
                                        text: qsTr("Open")
                                        style: "primary"
                                        Accessible.role: Accessible.Button
                                        Accessible.name: qsTr("Open %1").arg(projectRow.modelData.title)
                                        onClicked: root.viewModel.open(projectRow.index)
                                    }
                                    DialogButton {
                                        objectName: "libraryFolderButton"
                                        visible: !projectRow.confirming
                                        height: 30
                                        text: qsTr("Show in Folder")
                                        style: "secondary"
                                        onClicked: root.viewModel.showInFolder(projectRow.index)
                                    }
                                    DialogButton {
                                        objectName: "libraryDeleteButton"
                                        visible: !projectRow.modelData.open
                                        height: 30
                                        text: projectRow.confirming ? qsTr("Delete") : qsTr("Delete...")
                                        style: projectRow.confirming ? "primary" : "secondary"
                                        Accessible.role: Accessible.Button
                                        Accessible.name: qsTr("Delete %1").arg(projectRow.modelData.title)
                                        onClicked: {
                                            if (projectRow.confirming) {
                                                root.confirmingIndex = -1
                                                root.viewModel.remove(projectRow.index)
                                            } else {
                                                root.confirmingIndex = projectRow.index
                                            }
                                        }
                                    }
                                    DialogButton {
                                        objectName: "libraryCancelDeleteButton"
                                        visible: projectRow.confirming
                                        height: 30
                                        text: qsTr("Cancel")
                                        style: "secondary"
                                        onClicked: root.confirmingIndex = -1
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
                    objectName: "libraryCloseButton"
                    text: qsTr("Close")
                    style: "secondary"
                    width: 100
                    onClicked: root.viewModel.close()
                }
            }
        }
    }
}
