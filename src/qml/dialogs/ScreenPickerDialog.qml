pragma ComponentBehavior: Bound

import QtQuick
import SnapTrayQml

DialogBase {
    id: root

    property var viewModel: null
    readonly property int cardWidth: 210
    readonly property int cardGap: 12
    readonly property int screenCount: viewModel ? viewModel.screens.length : 0

    title: qsTr("Record Screen")
    subtitle: ""
    iconText: "\u23FA"
    dialogWidth: Math.min(704, Math.max(420, 28 + screenCount * cardWidth + Math.max(0, screenCount - 1) * cardGap))

    function dismiss() {
        if (viewModel) {
            viewModel.cancel()
            return
        }

        const window = root.Window.window
        if (window) {
            window.close()
        }
    }

    onCloseRequested: dismiss()

    contentItem: Component {
        Item {
            implicitHeight: contentColumn.implicitHeight + 28

            Column {
                id: contentColumn
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Flow {
                    width: parent.width
                    spacing: root.cardGap

                    Repeater {
                        model: root.viewModel ? root.viewModel.screens : []

                        ThumbnailCard {
                            required property var modelData
                            required property int index

                            thumbnailUrl: modelData.hasThumbnail
                                ? "image://dialog/" + modelData.thumbnailId + "?" + root.viewModel.thumbnailCacheBuster
                                : ""
                            titleText: modelData.name
                            subtitleText: modelData.resolution
                            tooltipText: modelData.tooltip
                            badgeText: ""
                            broken: !modelData.hasThumbnail
                            onClicked: if (root.viewModel) root.viewModel.chooseScreen(index)
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
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8
                layoutDirection: Qt.RightToLeft

                DialogButton {
                    text: qsTr("Cancel")
                    style: "secondary"
                    width: 100
                    onClicked: root.dismiss()
                }
            }
        }
    }
}
