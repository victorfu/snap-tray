import QtQuick
import SnapTrayQml

/**
 * ArrowStyleSection: Arrow end-style selector button that opens a native QMenu.
 */
Item {
    id: root
    objectName: "arrowStyleSection"
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
        root.viewModel.handleArrowStyleDropdown(mapped.x, mapped.y)
        root.menuClosed()
    }

    component ArrowPreview: Canvas {
        required property int styleValue
        required property color strokeColor
        required property color backgroundColor

        width: parent ? parent.width - 16 : 36
        height: 14
        antialiasing: true
        renderTarget: Canvas.Image

        function drawArrowTriangle(ctx, tipX, tipY, pointRight, filled) {
            var arrowLength = 6
            var arrowHalfHeight = 3.5

            ctx.beginPath()
            ctx.moveTo(tipX, tipY)
            if (pointRight) {
                ctx.lineTo(tipX - arrowLength, tipY - arrowHalfHeight)
                ctx.lineTo(tipX - arrowLength, tipY + arrowHalfHeight)
            } else {
                ctx.lineTo(tipX + arrowLength, tipY - arrowHalfHeight)
                ctx.lineTo(tipX + arrowLength, tipY + arrowHalfHeight)
            }
            ctx.closePath()

            if (filled) {
                ctx.fill()
            } else {
                var previousLineWidth = ctx.lineWidth
                var previousFillStyle = ctx.fillStyle
                ctx.fillStyle = backgroundColor
                ctx.fill()
                ctx.lineWidth = Math.max(1.15, previousLineWidth - 0.45)
                ctx.stroke()
                ctx.lineWidth = previousLineWidth
                ctx.fillStyle = previousFillStyle
            }
        }

        function drawArrowLine(ctx, tipX, tipY, pointRight) {
            var arrowLength = 6
            var arrowHalfHeight = 3.5
            var backX = pointRight ? tipX - arrowLength : tipX + arrowLength

            ctx.beginPath()
            ctx.moveTo(backX, tipY - arrowHalfHeight)
            ctx.lineTo(tipX, tipY)
            ctx.lineTo(backX, tipY + arrowHalfHeight)
            ctx.stroke()
        }

        onStyleValueChanged: requestPaint()
        onStrokeColorChanged: requestPaint()
        onBackgroundColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.lineWidth = 1.8
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.strokeStyle = strokeColor
            ctx.fillStyle = strokeColor

            var hasEndArrow = styleValue !== 0
            var hasStartArrow = styleValue === 4 || styleValue === 5
            var centerY = height / 2
            var lineStartX = hasStartArrow ? 8 : 2
            var lineEndX = hasEndArrow ? width - 8 : width - 2

            ctx.beginPath()
            ctx.moveTo(lineStartX, centerY)
            ctx.lineTo(lineEndX, centerY)
            ctx.stroke()

            switch (styleValue) {
            case 1:
                drawArrowTriangle(ctx, width - 2, centerY, true, true)
                break
            case 2:
                drawArrowTriangle(ctx, width - 2, centerY, true, false)
                break
            case 3:
                drawArrowLine(ctx, width - 2, centerY, true)
                break
            case 4:
                drawArrowTriangle(ctx, 2, centerY, false, true)
                drawArrowTriangle(ctx, width - 2, centerY, true, true)
                break
            case 5:
                drawArrowTriangle(ctx, 2, centerY, false, false)
                drawArrowTriangle(ctx, width - 2, centerY, true, false)
                break
            }
        }

        Component.onCompleted: requestPaint()
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

        ArrowPreview {
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            styleValue: root.hasViewModel ? root.viewModel.arrowStyle : 0
            strokeColor: ComponentTokens.toolbarIcon
            backgroundColor: buttonRect.color
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
            cursorShape: Qt.ArrowCursor

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
