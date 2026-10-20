import QtQuick
import SnapTrayQml

/**
 * DelayedCaptureIndicator: compact glass pill that shows the seconds left
 * before a delayed capture grabs the screen.
 *
 * The hosting window is click-through and never takes focus, so the user can
 * open menus and hover UI while it counts down. It is hidden before the grab
 * so it never appears in the capture.
 *
 * Properties set from C++:
 *   remainingSeconds — seconds left in the countdown
 */
Item {
    id: root

    property int remainingSeconds: 0

    implicitWidth: pill.width
    implicitHeight: pill.height

    Rectangle {
        id: pill

        width: content.implicitWidth + ComponentTokens.badgePaddingH * 3
        height: content.implicitHeight + ComponentTokens.badgePaddingV * 2
        radius: height / 2
        color: ComponentTokens.badgeBackground

        // Subtle inner highlight for glass depth
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: ComponentTokens.badgeHighlight
        }

        // Hairline border
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: 1
            border.color: ComponentTokens.badgeBorder
        }

        Row {
            id: content
            anchors.centerIn: parent
            spacing: ComponentTokens.badgePaddingH

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.remainingSeconds
                color: ComponentTokens.badgeText
                font.pixelSize: ComponentTokens.badgeFontSize * 2
                font.weight: Font.Bold
                font.family: SemanticTokens.fontFamily
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Delayed capture")
                color: ComponentTokens.badgeText
                font.pixelSize: ComponentTokens.badgeFontSize
                font.family: SemanticTokens.fontFamily
            }
        }
    }
}
