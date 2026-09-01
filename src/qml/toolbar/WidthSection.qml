import QtQuick
import SnapTrayQml

/**
 * WidthSection: Stroke width preview and Mosaic width presets.
 *
 * Non-Mosaic tools use the original 28x28 preview. Mosaic replaces it with
 * three common-width shortcuts.
 */
Item {
    id: root
    objectName: "widthSection"
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property int currentWidthValue: root.hasViewModel ? root.viewModel.currentWidth : 1
    readonly property int minWidthValue: root.hasViewModel ? root.viewModel.minWidth : 1
    readonly property int maxWidthValue: root.hasViewModel ? root.viewModel.maxWidth : 1
    readonly property bool mosaicActive: root.hasViewModel && root.viewModel.mosaicActive
    readonly property var mosaicWidthPresetModel: root.hasViewModel
                                                    ? root.viewModel.mosaicWidthPresetOptions
                                                    : []
    readonly property int controlSpacing: 2
    readonly property int sectionPadding: 3
    readonly property int sectionTrailingPadding: 1

    implicitWidth: root.mosaicActive
        ? root.sectionPadding + mosaicWidthPresetRow.width + root.sectionTrailingPadding
        : previewSlot.width
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight

    Row {
        anchors.left: parent.left
        anchors.leftMargin: root.mosaicActive ? root.sectionPadding : 0
        anchors.verticalCenter: parent.verticalCenter
        height: root.height
        spacing: root.controlSpacing

        Item {
            id: previewSlot
            objectName: "widthPreviewSlot"
            visible: !root.mosaicActive
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
                color: DesignSystem.accentDefault

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

                        // Let ToolOptionsStrip consume Mosaic wheel events.
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
