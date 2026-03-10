import QtQuick
import SnapTrayQml

Item {
    id: root

    required property QtObject viewModel

    width: 280

    // Theme-adaptive colors (follows toolbar style)
    readonly property color titleColor: SemanticTokens.textPrimary
    readonly property color primaryText: SemanticTokens.textPrimary
    readonly property color secondaryText: SemanticTokens.textSecondary
    readonly property color hoverBg: ComponentTokens.toolbarControlBackgroundHover
    readonly property color activeBg: SemanticTokens.accentDefault
    readonly property color activeText: SemanticTokens.textOnAccent
    readonly property color borderColor: SemanticTokens.borderDefault
    readonly property color handleColor: SemanticTokens.textTertiary

    opacity: 0
    Component.onCompleted: fadeIn.start()

    NumberAnimation on opacity {
        id: fadeIn
        from: 0; to: 1
        duration: SemanticTokens.durationFast
        easing.type: Easing.OutCubic
        running: false
    }

    GlassSurface {
        anchors.fill: parent
        glassRadius: SemanticTokens.radiusMedium
    }

    Column {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacing8
        spacing: SemanticTokens.spacing8

        // Title
        Text {
            text: qsTr("Regions")
            color: root.titleColor
            font.pixelSize: SemanticTokens.fontSizeBody
            font.weight: Font.DemiBold
            font.family: SemanticTokens.fontFamily
            leftPadding: 2
        }

        // Region list
        ListView {
            id: regionList
            width: parent.width
            height: parent.height - 30 - parent.spacing
            model: root.viewModel.regions
            spacing: PrimitiveTokens.spacing4
            clip: true

            moveDisplaced: Transition {
                NumberAnimation { properties: "y"; duration: SemanticTokens.durationFast; easing.type: Easing.OutCubic }
            }

            delegate: Item {
                id: delegateRoot
                width: regionList.width
                height: 72

                required property var modelData
                required property int index

                property bool held: false
                property int dragStartIndex: index
                property bool isActive: root.viewModel.activeIndex === delegateRoot.index
                z: held ? 10 : 0

                Rectangle {
                    id: itemBg
                    anchors.fill: parent
                    radius: PrimitiveTokens.radiusSmall
                    color: delegateRoot.isActive
                        ? root.activeBg
                        : (delegateRoot.held || delegateHover.hovered)
                            ? root.hoverBg
                            : "transparent"
                    Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
                }

                Row {
                    anchors.fill: parent
                    anchors.margins: SemanticTokens.spacing8
                    spacing: SemanticTokens.spacing8

                    // Drag handle
                    Item {
                        width: 16; height: parent.height

                        Column {
                            anchors.centerIn: parent
                            spacing: 3
                            Repeater {
                                model: 3
                                Rectangle {
                                    width: 12; height: 2; radius: 1
                                    color: delegateRoot.isActive ? root.activeText : root.handleColor
                                    opacity: 0.5
                                }
                            }
                        }

                        MouseArea {
                            id: dragArea
                            anchors.fill: parent
                            cursorShape: Qt.OpenHandCursor

                            property real startMouseY: 0

                            onPressed: function(mouse) {
                                startMouseY = mapToItem(regionList, 0, mouse.y).y
                                delegateRoot.held = true
                                delegateRoot.dragStartIndex = delegateRoot.index
                            }
                            onPositionChanged: function(mouse) {
                                if (!delegateRoot.held) return
                                delegateRoot.z = 10
                            }
                            onReleased: function(mouse) {
                                if (!delegateRoot.held) return
                                delegateRoot.held = false
                                delegateRoot.z = 0
                                let currentY = mapToItem(regionList, 0, mouse.y).y
                                let delta = currentY - startMouseY
                                let itemH = 72 + regionList.spacing
                                let steps = Math.round(delta / itemH)
                                let targetIndex = Math.max(0, Math.min(delegateRoot.dragStartIndex + steps, root.viewModel.count - 1))
                                if (targetIndex !== delegateRoot.dragStartIndex) {
                                    root.viewModel.moveRegion(delegateRoot.dragStartIndex, targetIndex)
                                }
                            }
                        }
                    }

                    // Thumbnail
                    Rectangle {
                        width: 72; height: 46
                        anchors.verticalCenter: parent.verticalCenter
                        radius: PrimitiveTokens.radiusSmall
                        color: "transparent"
                        border.width: 1
                        border.color: delegateRoot.isActive
                            ? Qt.rgba(root.activeText.r, root.activeText.g, root.activeText.b, 0.3)
                            : root.borderColor
                        clip: true

                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            source: "image://dialog/" + delegateRoot.modelData.thumbnailId + "?" + root.viewModel.thumbnailCacheBuster
                            fillMode: Image.PreserveAspectCrop
                            cache: false
                        }
                    }

                    // Text column
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        width: parent.width - 16 - 72 - 56 - 4 * SemanticTokens.spacing8

                        Text {
                            text: qsTr("Region %1").arg(delegateRoot.modelData.index)
                            color: delegateRoot.isActive ? root.activeText : root.primaryText
                            font.pixelSize: SemanticTokens.fontSizeBody
                            font.weight: Font.DemiBold
                            font.family: SemanticTokens.fontFamily
                            elide: Text.ElideRight
                            width: parent.width
                            Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
                        }

                        Text {
                            text: delegateRoot.modelData.width + " \u00D7 " + delegateRoot.modelData.height
                            color: delegateRoot.isActive
                                ? Qt.rgba(root.activeText.r, root.activeText.g, root.activeText.b, 0.7)
                                : root.secondaryText
                            font.pixelSize: SemanticTokens.fontSizeCaption
                            font.family: SemanticTokens.fontFamily
                            Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }
                        }
                    }

                    // Action buttons
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: PrimitiveTokens.spacing4

                        // Replace button
                        Rectangle {
                            width: 56; height: 24
                            radius: PrimitiveTokens.radiusSmall
                            color: replaceTap.pressed
                                ? ComponentTokens.buttonGhostBackgroundPressed
                                : replaceHover.hovered
                                    ? ComponentTokens.buttonGhostBackgroundHover
                                    : "transparent"
                            border.width: 1
                            border.color: delegateRoot.isActive
                                ? Qt.rgba(root.activeText.r, root.activeText.g, root.activeText.b, 0.3)
                                : root.borderColor
                            Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Replace")
                                color: delegateRoot.isActive ? root.activeText : root.primaryText
                                font.pixelSize: PrimitiveTokens.fontSizeSmall
                                font.family: SemanticTokens.fontFamily
                            }
                            HoverHandler { id: replaceHover }
                            TapHandler { id: replaceTap; onTapped: root.viewModel.replaceRegion(delegateRoot.index) }
                        }

                        // Delete button
                        Rectangle {
                            width: 56; height: 24
                            radius: PrimitiveTokens.radiusSmall
                            color: deleteTap.pressed
                                ? ComponentTokens.buttonGhostBackgroundPressed
                                : deleteHover.hovered
                                    ? ComponentTokens.buttonGhostBackgroundHover
                                    : "transparent"
                            border.width: 1
                            border.color: delegateRoot.isActive
                                ? Qt.rgba(root.activeText.r, root.activeText.g, root.activeText.b, 0.3)
                                : root.borderColor
                            Behavior on color { ColorAnimation { duration: SemanticTokens.durationInteractionHover } }

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Delete")
                                color: delegateRoot.isActive ? root.activeText : root.primaryText
                                font.pixelSize: PrimitiveTokens.fontSizeSmall
                                font.family: SemanticTokens.fontFamily
                            }
                            HoverHandler { id: deleteHover }
                            TapHandler { id: deleteTap; onTapped: root.viewModel.deleteRegion(delegateRoot.index) }
                        }
                    }
                }

                HoverHandler { id: delegateHover }
                TapHandler { onTapped: root.viewModel.activateRegion(delegateRoot.index) }
            }
        }
    }
}
