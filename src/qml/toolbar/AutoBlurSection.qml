import QtQuick
import SnapTrayQml

/**
 * AutoBlurSection: Single button to trigger auto-blur sensitive data.
 */
Item {
    id: root
    property var viewModel: null
    property bool hintActive: false
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property bool autoBlurProcessingValue: root.hasViewModel && root.viewModel.autoBlurProcessing
    signal buttonHovered(real anchorX, real anchorY, real anchorW, real anchorH)
    signal buttonHoverExited()

    implicitWidth: 22
    implicitHeight: 22
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        id: autoBlurButton
        objectName: "autoBlurButton"
        anchors.fill: parent
        radius: 4
        readonly property bool emphasized: root.hintActive || blurMouse.containsMouse
        color: {
            if (root.autoBlurProcessingValue)
                return "#FFC107"  // Material amber — processing indicator
            if (emphasized)
                return SemanticTokens.isDarkMode
                    ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
                    : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)
            return "transparent"
        }
        border.width: root.hintActive ? 1 : 0
        border.color: DesignSystem.accentDefault
        scale: root.hintActive ? 1.06 : 1.0

        Behavior on color {
            ColorAnimation { duration: 100 }
        }

        Behavior on scale {
            NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
        }

        SvgIcon {
            anchors.centerIn: parent
            source: "qrc:/icons/icons/auto-blur.svg"
            iconSize: 14
            color: root.autoBlurProcessingValue
                ? "#000000"
                : ComponentTokens.toolbarIcon
            opacity: root.autoBlurProcessingValue ? 1.0 : (blurMouse.containsMouse ? 1.0 : 0.6)
        }

        MouseArea {
            id: blurMouse
            anchors.fill: parent
            cursorShape: CursorTokens.toolbarControl
            hoverEnabled: true
            onEntered: {
                var anchor = autoBlurButton.mapToGlobal(0, 0)
                root.buttonHovered(anchor.x, anchor.y,
                                   autoBlurButton.width,
                                   autoBlurButton.height)
            }
            onExited: root.buttonHoverExited()
            onClicked: {
                if (root.hasViewModel)
                    root.viewModel.handleAutoBlurClicked()
            }
        }
    }
}
