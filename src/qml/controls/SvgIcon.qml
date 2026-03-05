import QtQuick
import SnapTrayQml

/**
 * SvgIcon: Reusable SVG icon renderer with token-driven tint.
 * Wraps SvgIconItem (C++) for stable cross-platform SVG rendering.
 */
Item {
    id: root

    property url source
    property color color: SemanticTokens.iconColor
    property int iconSize: ComponentTokens.iconSizeToolbar

    width: iconSize
    height: iconSize

    SvgIconItem {
        anchors.fill: parent
        source: root.source
        color: root.color
        visible: root.source !== ""
    }
}
