import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

Item {
    id: root

    readonly property var modelObject: typeof pinHistoryModel !== "undefined" ? pinHistoryModel : null
    readonly property var backend: typeof pinHistoryBackend !== "undefined" ? pinHistoryBackend : null
    property int contextIndex: -1

    Rectangle {
        anchors.fill: parent
        color: SemanticTokens.backgroundPrimary
    }

    PanelSurface {
        anchors.fill: parent
        anchors.margins: 12
        radius: 14
    }

    Column {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Row {
            width: parent.width
            height: 32
            spacing: 12

            Text {
                text: qsTr("Pin History")
                anchors.verticalCenter: parent.verticalCenter
                color: SemanticTokens.textPrimary
                font.pixelSize: SemanticTokens.fontSizeH2
                font.family: SemanticTokens.fontFamily
                font.weight: SemanticTokens.fontWeightSemiBold
            }

            Item {
                width: Math.max(0, parent.width - folderButton.width - 120)
                height: 1
            }

            SettingsButton {
                id: folderButton
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Open Cache Folder")
                onClicked: if (root.backend) root.backend.openCacheFolder()
            }
        }

        Text {
            visible: root.modelObject && root.modelObject.count === 0
            width: parent.width
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: qsTr("No pinned screenshots yet")
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeBody
            font.family: SemanticTokens.fontFamily
        }

        GridView {
            id: grid
            visible: root.modelObject && root.modelObject.count > 0
            width: parent.width
            height: parent.height - 48
            cellWidth: 210
            cellHeight: 165
            clip: true
            model: root.modelObject
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: ThumbnailCard {
                thumbnailUrl: model.thumbnailUrl
                titleText: model.capturedAtText
                subtitleText: model.sizeText
                tooltipText: model.tooltipText
                selected: grid.currentIndex === model.index

                onActivated: {
                    grid.currentIndex = model.index
                    if (root.backend)
                        root.backend.pin(model.index)
                }

                onContextMenuRequested: function(globalX, globalY) {
                    grid.currentIndex = model.index
                    root.contextIndex = model.index
                    var localPoint = root.mapFromGlobal(globalX, globalY)
                    contextMenu.x = Math.min(localPoint.x, root.width - contextMenu.width - 8)
                    contextMenu.y = Math.min(localPoint.y, root.height - contextMenu.height - 8)
                    contextMenu.open()
                }
            }
        }
    }

    ContextActionMenu {
        id: contextMenu
        model: [
            { id: "pin", text: qsTr("Pin"), icon: "qrc:/icons/icons/pin.svg" },
            { id: "copy", text: qsTr("Copy"), icon: "qrc:/icons/icons/copy.svg" },
            { id: "save", text: qsTr("Save As"), icon: "qrc:/icons/icons/save.svg" },
            { separator: true },
            { id: "delete", text: qsTr("Delete"), icon: "qrc:/icons/icons/trash-2.svg" }
        ]

        onActionTriggered: function(actionId) {
            if (!root.backend || root.contextIndex < 0)
                return

            if (actionId === "pin")
                root.backend.pin(root.contextIndex)
            else if (actionId === "copy")
                root.backend.copy(root.contextIndex)
            else if (actionId === "save")
                root.backend.saveAs(root.contextIndex)
            else if (actionId === "delete")
                root.backend.deleteEntry(root.contextIndex)
        }
    }
}
