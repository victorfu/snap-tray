import QtQuick
import SnapTrayQml

/**
 * AutoBlurSection: Single button to trigger auto-blur sensitive data.
 */
Item {
    id: root
    required property var viewModel

    implicitWidth: 22
    implicitHeight: 22
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        radius: 4
        color: {
            if (root.viewModel.autoBlurProcessing)
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
            color: root.viewModel.autoBlurProcessing
                ? "#000000"
                : ComponentTokens.toolbarIcon
            opacity: root.viewModel.autoBlurProcessing ? 1.0 : (blurMouse.containsMouse ? 1.0 : 0.6)
        }

        MouseArea {
            id: blurMouse
            anchors.fill: parent
            cursorShape: Qt.ArrowCursor
            hoverEnabled: true
            onClicked: root.viewModel.handleAutoBlurClicked()
        }
    }
}
