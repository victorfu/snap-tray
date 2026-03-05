import QtQuick
import Qt5Compat.GraphicalEffects
import SnapTrayQml

/**
 * SvgIcon: Reusable SVG icon renderer with token-driven ColorOverlay tint.
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
    }

    ColorOverlay {
        anchors.fill: svgImage
        source: svgImage
        color: root.color
        visible: root.source !== ""
    }
}
