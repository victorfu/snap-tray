import QtQuick
import SnapTrayQml

/**
 * SvgIcon: Reusable SVG icon renderer with token-driven tint.
 */
Item {
    id: root

    property url source
    property color color: SemanticTokens.iconColor
    property int iconSize: ComponentTokens.iconSizeToolbar

    width: iconSize
    height: iconSize

    Image {
        id: svgImage
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.iconSize
        sourceSize.height: root.iconSize
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        visible: false
        onStatusChanged: canvas.requestPaint()
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        visible: root.source !== ""

        onPaint: {
            const context = getContext("2d");
            context.reset();

            if (svgImage.status !== Image.Ready) {
                return;
            }

            context.drawImage(svgImage, 0, 0, width, height);
            context.globalCompositeOperation = "source-in";
            context.fillStyle = root.color;
            context.fillRect(0, 0, width, height);
            context.globalCompositeOperation = "source-over";
        }
    }

    onColorChanged: canvas.requestPaint()
    onSourceChanged: canvas.requestPaint()
    onIconSizeChanged: canvas.requestPaint()
}
