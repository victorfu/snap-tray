import QtQuick
import SnapTrayQml

/**
 * LineStyleSection: Line style selector button (solid/dashed/dotted) that opens a native QMenu.
 */
Item {
    id: root
    objectName: "lineStyleSection"
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    signal menuOpened()
    signal menuClosed()

    readonly property int buttonWidth: 52
    readonly property int buttonHeight: 20
    property int hoveredOption: -1

    implicitWidth: buttonWidth
    implicitHeight: 22
    width: implicitWidth
    height: implicitHeight

    readonly property color btnInactiveBg: SemanticTokens.isDarkMode
        ? Qt.rgba(50 / 255, 50 / 255, 50 / 255, 1.0)
        : Qt.rgba(245 / 255, 245 / 255, 245 / 255, 1.0)
    readonly property color btnHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)

    function closeMenu() {
        hoveredOption = -1
    }

    function openMenu() {
        if (!root.hasViewModel)
            return
        var mapped = buttonRect.mapToGlobal(0, buttonRect.height)
        root.menuOpened()
        root.viewModel.handleLineStyleDropdown(mapped.x, mapped.y)
        root.menuClosed()
    }

    function lineDashPattern(styleValue) {
        if (styleValue === 1)
            return [6, 4]
        if (styleValue === 2)
            return [1, 4]
        return []
    }

    component LinePreview: Canvas {
        required property int styleValue
        required property color strokeColor

        width: parent ? parent.width - 16 : 36
        height: 14

        onStyleValueChanged: requestPaint()
        onStrokeColorChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.lineWidth = 2
            ctx.strokeStyle = strokeColor
            ctx.lineCap = "round"
            ctx.setLineDash(root.lineDashPattern(styleValue))
            ctx.beginPath()
            ctx.moveTo(2, height / 2)
            ctx.lineTo(width - 2, height / 2)
            ctx.stroke()
        }
    }

    Rectangle {
        id: buttonRect
        y: 1
        width: root.buttonWidth
        height: root.buttonHeight
        radius: 4
        color: root.hoveredOption === -2 ? root.btnHoverBg : root.btnInactiveBg
        border.width: 1
        border.color: SemanticTokens.borderDefault

        LinePreview {
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            styleValue: root.hasViewModel ? root.viewModel.lineStyle : 0
            strokeColor: ComponentTokens.toolbarIcon
        }

        ToolbarChevron {
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            strokeColor: ComponentTokens.toolbarIcon
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: CursorTokens.toolbarControl

            onContainsMouseChanged: {
                root.hoveredOption = containsMouse ? -2 : -1
            }

            onClicked: {
                root.openMenu()
                root.hoveredOption = containsMouse ? -2 : -1
            }
        }
    }
}
