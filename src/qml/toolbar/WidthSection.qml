import QtQuick
import SnapTrayQml

/**
 * WidthSection: Stroke width preview and Mosaic width presets.
 *
 * The blue preview remains in its original 28x28 slot. Mosaic adds three
 * common-width shortcuts to its right, while the containing strip continues
 * to own mouse-wheel adjustment.
 */
Item {
    id: root
    objectName: "widthSection"
    property var viewModel: null
    property bool hintActive: false
    property bool hoverHintEnabled: false
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property int currentWidthValue: root.hasViewModel ? root.viewModel.currentWidth : 1
    readonly property int minWidthValue: root.hasViewModel ? root.viewModel.minWidth : 1
    readonly property int maxWidthValue: root.hasViewModel ? root.viewModel.maxWidth : 1
    readonly property bool mosaicActive: root.hasViewModel && root.viewModel.mosaicActive
    readonly property var mosaicWidthPresetModel: root.hasViewModel
                                                    ? root.viewModel.mosaicWidthPresetOptions
                                                    : []
    readonly property int controlSpacing: 2
    signal previewHovered(real anchorX, real anchorY, real anchorW, real anchorH)
    signal previewHoverExited()

    implicitWidth: previewSlot.width
                   + (root.mosaicActive
                      ? root.controlSpacing + mosaicWidthPresetRow.width
                      : 0)
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight

    Row {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.controlSpacing

        Item {
            id: previewSlot
            objectName: "widthPreviewSlot"
            width: 28
            height: 28

            // Blue container
            Rectangle {
                id: widthPreviewContainer
                objectName: "widthPreviewContainer"
                anchors.centerIn: parent
                width: 22
                height: 22
                radius: 5
                readonly property bool emphasized: root.hintActive
                                                   || (root.hoverHintEnabled && previewHover.hovered)
                color: emphasized ? DesignSystem.accentHover : DesignSystem.accentDefault
                border.width: emphasized ? 1 : 0
                border.color: DesignSystem.textOnAccent
                scale: emphasized ? 1.06 : 1.0

                Behavior on color {
                    ColorAnimation { duration: 100 }
                }

                Behavior on scale {
                    NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
                }

                HoverHandler {
                    id: previewHover
                    objectName: "widthPreviewHoverHandler"
                    enabled: root.hoverHintEnabled

                    onHoveredChanged: {
                        if (hovered) {
                            var anchor = widthPreviewContainer.mapToGlobal(0, 0)
                            root.previewHovered(anchor.x, anchor.y,
                                                widthPreviewContainer.width,
                                                widthPreviewContainer.height)
                        } else {
                            root.previewHoverExited()
                        }
                    }
                }

                // Width preview dot
                Rectangle {
                    id: widthDot
                    objectName: "widthPreviewDot"

                    readonly property real minDot: 4
                    readonly property real maxDot: 20
                    readonly property real ratio: (root.currentWidthValue - root.minWidthValue) /
                                                  Math.max(1, root.maxWidthValue - root.minWidthValue)

                    width: Math.round(minDot + ratio * (maxDot - minDot))
                    height: width
                    x: (parent.width - width) / 2
                    y: (parent.height - height) / 2
                    radius: width / 2
                    color: "white"
                }
            }
        }

        Row {
            id: mosaicWidthPresetRow
            objectName: "mosaicWidthPresetRow"
            visible: root.mosaicActive
            spacing: root.controlSpacing
            anchors.verticalCenter: parent.verticalCenter

            Repeater {
                model: root.mosaicWidthPresetModel

                Rectangle {
                    required property var modelData

                    objectName: "mosaicWidthPresetButton_" + modelData.value
                    width: 22
                    height: 22
                    radius: 4
                    readonly property bool selected: root.currentWidthValue === modelData.value
                    color: selected ? DesignSystem.accentDefault
                                    : presetMouse.containsMouse ? root.btnHoverBg
                                    : "transparent"

                    Rectangle {
                        objectName: "mosaicWidthPresetDot_" + modelData.value
                        anchors.centerIn: parent
                        width: modelData.previewDiameter
                        height: width
                        radius: width / 2
                        color: parent.selected
                            ? ComponentTokens.toolbarIconActive
                            : ComponentTokens.toolbarIcon
                    }

                    MouseArea {
                        id: presetMouse
                        objectName: "mosaicWidthPresetMouseArea_" + modelData.value
                        anchors.fill: parent
                        cursorShape: CursorTokens.toolbarControl
                        hoverEnabled: true

                        onClicked: {
                            if (root.hasViewModel)
                                root.viewModel.handleMosaicWidthPresetSelected(modelData.value)
                        }

                        // Keep width adjustment owned by ToolOptionsStrip's
                        // strip-wide wheel handler, including over presets.
                        onWheel: function(wheel) {
                            wheel.accepted = false
                        }
                    }
                }
            }
        }
    }

    readonly property color btnHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)
}
