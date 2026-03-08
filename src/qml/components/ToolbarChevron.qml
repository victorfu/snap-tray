import QtQuick
import SnapTrayQml

Canvas {
    id: root

    property color strokeColor: ComponentTokens.toolbarIcon
    property real strokeWidth: 1.25
    property int chevronWidth: 6
    property int chevronHeight: 4

    implicitWidth: 8
    implicitHeight: 6
    width: implicitWidth
    height: implicitHeight
    antialiasing: true
    renderTarget: Canvas.Image

    onStrokeColorChanged: requestPaint()
    onStrokeWidthChanged: requestPaint()
    onChevronWidthChanged: requestPaint()
    onChevronHeightChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = strokeColor

        var startX = (width - chevronWidth) / 2
        var startY = (height - chevronHeight) / 2

        ctx.beginPath()
        ctx.moveTo(startX, startY)
        ctx.lineTo(startX + chevronWidth, startY)
        ctx.lineTo(startX + chevronWidth / 2, startY + chevronHeight)
        ctx.closePath()
        ctx.fill()
    }

    Component.onCompleted: requestPaint()
}
