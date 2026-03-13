import QtQuick
import SnapTrayQml

/**
 * AutoBlurSection: Single button to trigger auto-blur sensitive data.
 */
Item {
    id: root
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property bool autoBlurProcessingValue: root.hasViewModel && root.viewModel.autoBlurProcessing

    implicitWidth: 22
    implicitHeight: 22
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: {
            if (root.autoBlurProcessingValue)
                return "#FFC107"  // Material amber — processing indicator
            if (blurMouse.containsMouse)
                return SemanticTokens.isDarkMode
                    ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
                    : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)
            return "transparent"
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
            onClicked: {
                if (root.hasViewModel)
                    root.viewModel.handleAutoBlurClicked()
            }
        }
    }
}
