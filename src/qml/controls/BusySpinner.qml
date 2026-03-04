import QtQuick
import SnapTrayQml

/**
 * BusySpinner: Lightweight animated spinner for async operations.
 *
 * Small rotating arc indicator. Set `running` to show/hide.
 */
Item {
    id: root

    property bool running: false
    property int size: 16
    property color color: SemanticTokens.textSecondary

    width: size
    height: size
    visible: running
    opacity: running ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: PrimitiveTokens.durationFast }
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        property real angle: 0

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = root.color
            ctx.lineWidth = 2
            ctx.lineCap = "round"

            var cx = width / 2
            var cy = height / 2
            var r = (Math.min(width, height) - 4) / 2

            ctx.beginPath()
            ctx.arc(cx, cy, r, angle, angle + Math.PI * 1.2)
            ctx.stroke()
        }

        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: root.running
        }

        // Redraw on rotation to show arc
        onRotationChanged: requestPaint()
        Component.onCompleted: requestPaint()
    }
}
