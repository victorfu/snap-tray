import QtQuick
import SnapTrayQml

/**
 * WidthSection: Stroke width preview circle.
 *
 * Blue square container with white dot that scales based on current width.
 * Mouse wheel adjusts width.
 */
Item {
    id: root
    property var viewModel: null
    property bool hintActive: false
    property bool hoverHintEnabled: false
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property int currentWidthValue: root.hasViewModel ? root.viewModel.currentWidth : 1
    readonly property int minWidthValue: root.hasViewModel ? root.viewModel.minWidth : 1
    readonly property int maxWidthValue: root.hasViewModel ? root.viewModel.maxWidth : 1
    signal previewHovered(real anchorX, real anchorY, real anchorW, real anchorH)
    signal previewHoverExited()

    implicitWidth: 28
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight

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
