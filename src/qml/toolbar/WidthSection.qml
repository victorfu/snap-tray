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
    required property var viewModel

    implicitWidth: 28
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight

    // Blue container
    Rectangle {
        x: 4
        y: 3
        width: 22
        height: 22
        radius: 5
        color: DesignSystem.accentDefault

        // Width preview dot
        Rectangle {
            id: widthDot
            anchors.centerIn: parent

            readonly property real minDot: 4
            readonly property real maxDot: 20
            readonly property real ratio: (root.viewModel.currentWidth - root.viewModel.minWidth) /
                                          Math.max(1, root.viewModel.maxWidth - root.viewModel.minWidth)

            width: minDot + ratio * (maxDot - minDot)
            height: width
            radius: width / 2
            color: "white"
        }
    }
}
