import QtQuick
import SnapTrayQml

/**
 * SettingsButton: Styled button with primary and secondary variants.
 *
 * Primary uses accent color background, secondary uses elevated surface with border.
 * Includes hover/press state animations.
 */
Rectangle {
    id: root

    property string text: ""
    property bool primary: false
    property bool enabled: true

    signal clicked()

    width: buttonText.implicitWidth + 32
    height: 32
    radius: PrimitiveTokens.radiusMedium
    color: {
        if (!root.enabled) return ComponentTokens.buttonDisabledBackground
        if (mouseArea.pressed) return root.primary
            ? ComponentTokens.buttonPrimaryBackgroundPressed
            : ComponentTokens.buttonSecondaryBackgroundPressed
        if (mouseArea.containsMouse) return root.primary
            ? ComponentTokens.buttonPrimaryBackgroundHover
            : ComponentTokens.buttonSecondaryBackgroundHover
        return root.primary
            ? ComponentTokens.buttonPrimaryBackground
            : ComponentTokens.buttonSecondaryBackground
    }

    border.width: root.primary ? 0 : 1
    border.color: !root.enabled ? ComponentTokens.buttonDisabledBorder
        : root.primary ? "transparent" : ComponentTokens.buttonSecondaryBorder

    Behavior on color {
        ColorAnimation { duration: PrimitiveTokens.durationFast }
    }

    Text {
        id: buttonText
        anchors.centerIn: parent
        text: root.text
        color: !root.enabled ? ComponentTokens.buttonDisabledText
            : root.primary
                ? ComponentTokens.buttonPrimaryText
                : ComponentTokens.buttonSecondaryText
        font.pixelSize: PrimitiveTokens.fontSizeBody
        font.family: PrimitiveTokens.fontFamily
        font.weight: Font.Medium
        font.letterSpacing: PrimitiveTokens.letterSpacingTight
    }

    FocusFrame {
        showFocus: root.activeFocus
        extraRadius: root.radius
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        hoverEnabled: true
        onClicked: {
            root.forceActiveFocus()
            root.clicked()
        }
    }

    Keys.onSpacePressed: if (root.enabled) root.clicked()
    Keys.onReturnPressed: if (root.enabled) root.clicked()
}
