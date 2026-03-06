import QtQuick
import SnapTrayQml

/**
 * FocusFrame: Visible keyboard focus ring overlay.
 *
 * Anchors to its parent and draws a rounded border when `showFocus` is true.
 * Fades in/out with a quick animation. Non-interactive (no mouse events).
 */
Rectangle {
    id: root

    property bool showFocus: false
    property int extraRadius: 0

    anchors.fill: parent
    anchors.margins: -ComponentTokens.focusRingOffset
    radius: (extraRadius > 0 ? extraRadius : ComponentTokens.focusRingRadius) + ComponentTokens.focusRingOffset
    color: "transparent"
    border.width: ComponentTokens.focusRingWidth
    border.color: ComponentTokens.focusRingColor
    visible: opacity > 0
    opacity: showFocus ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: SemanticTokens.durationFast }
    }
}
