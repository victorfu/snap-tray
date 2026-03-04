import QtQuick
import SnapTrayQml

/**
 * Badge: Lightweight glass-effect badge for transient indicators (zoom, opacity).
 *
 * Single-line centered text, no icon, no accent strip.
 * Shares the glass visual language with Toast but is smaller and simpler.
 *
 * Properties set from C++:
 *   badgeText     — the text to display
 *   badgeVisible  — triggers show/hide animations
 *   duration      — auto-hide delay in ms (default 1500)
 */
Item {
    id: root

    // Public properties (set from C++ bridge)
    property string badgeText: ""
    property bool badgeVisible: false
    property int duration: ComponentTokens.badgeDisplayDuration

    // Implicit size follows the badge content
    implicitWidth: badge.width
    implicitHeight: badge.height

    // Signals
    signal retrigger()   // C++ calls this to update text without re-fading
    signal hidden()      // Emitted when fade-out animation finishes

    onBadgeVisibleChanged: {
        if (badgeVisible) {
            hideTimer.stop();
            fadeOut.stop();
            badge.opacity = 1.0;
            fadeIn.start();
            badge.visible = true;
            hideTimer.interval = duration;
            hideTimer.start();
        } else {
            hideTimer.stop();
            fadeIn.stop();
            fadeOut.start();
        }
    }

    onRetrigger: {
        // Update text without re-fading; just restart the hide timer
        hideTimer.stop();
        hideTimer.interval = duration;
        hideTimer.start();
    }

    Timer {
        id: hideTimer
        interval: root.duration
        repeat: false
        onTriggered: {
            fadeIn.stop();
            fadeOut.start();
        }
    }

    Rectangle {
        id: badge
        visible: false
        opacity: 0.0

        width: textLabel.implicitWidth + ComponentTokens.badgePaddingH * 2
        height: textLabel.implicitHeight + ComponentTokens.badgePaddingV * 2
        radius: ComponentTokens.badgeRadius
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

        Text {
            id: textLabel
            anchors.centerIn: parent
            text: root.badgeText
            color: ComponentTokens.badgeText
            font.pixelSize: ComponentTokens.badgeFontSize
            font.family: PrimitiveTokens.fontFamily
        }

        NumberAnimation {
            id: fadeIn
            target: badge
            property: "opacity"
            from: 0.0
            to: 1.0
            duration: ComponentTokens.badgeFadeInDuration
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            id: fadeOut
            target: badge
            property: "opacity"
            from: badge.opacity
            to: 0.0
            duration: ComponentTokens.badgeFadeOutDuration
            easing.type: Easing.InCubic
            onFinished: {
                badge.visible = false;
                root.hidden();
            }
        }
    }
}
