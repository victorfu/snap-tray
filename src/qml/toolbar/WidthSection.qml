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
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property int currentWidthValue: root.hasViewModel ? root.viewModel.currentWidth : 1
    readonly property int minWidthValue: root.hasViewModel ? root.viewModel.minWidth : 1
    readonly property int maxWidthValue: root.hasViewModel ? root.viewModel.maxWidth : 1

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
            readonly property real ratio: (root.currentWidthValue - root.minWidthValue) /
                                          Math.max(1, root.maxWidthValue - root.minWidthValue)

            width: Math.round(minDot + ratio * (maxDot - minDot))
            height: width
            radius: width / 2
            color: "white"
        }
    }
}
