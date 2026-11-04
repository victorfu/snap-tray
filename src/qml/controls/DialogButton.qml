import QtQuick
import SnapTrayQml

Rectangle {
    id: root

    property string text: ""
    property string style: "secondary"  // "primary", "secondary", "ghost"
    property string feedbackText: ""
    property bool feedbackActive: false

    activeFocusOnTab: enabled
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (enabled) clicked()
    // Like a native button: Space activates on release, Return/Enter once per
    // press. Auto-repeat from a held key never activates again, so one long
    // press cannot both ask for and confirm a destructive action.
    property bool spaceArmed: false
    onActiveFocusChanged: if (!activeFocus) spaceArmed = false
    Keys.onPressed: function(event) {
        const space = event.key === Qt.Key_Space
        const confirm = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
        if (!space && !confirm) return
        event.accepted = true
        if (event.isAutoRepeat || !enabled) return
        if (space) spaceArmed = true
        else clicked()
    }
    Keys.onReleased: function(event) {
        if (event.key !== Qt.Key_Space) return
        event.accepted = true
        if (event.isAutoRepeat) return
        const armed = spaceArmed
        spaceArmed = false
        if (armed && enabled) clicked()
    }
    FocusFrame { anchors.fill: parent; showFocus: root.activeFocus }

    signal clicked()

    width: implicitWidth
    height: 40
    implicitWidth: Math.max(80, label.implicitWidth + 24)
    radius: DesignSystem.radiusMedium

    color: {
        if (!root.enabled) return ComponentTokens.buttonDisabledBackground
        if (feedbackActive) return DesignSystem.statusSuccess

        if (root.style === "primary") {
            if (mouseArea.pressed) return ComponentTokens.buttonPrimaryBackgroundPressed
            if (mouseArea.containsMouse) return ComponentTokens.buttonPrimaryBackgroundHover
            return ComponentTokens.buttonPrimaryBackground
        }
        if (root.style === "ghost") {
            if (mouseArea.pressed) return ComponentTokens.buttonGhostBackgroundPressed
            if (mouseArea.containsMouse) return ComponentTokens.buttonGhostBackgroundHover
            return ComponentTokens.buttonGhostBackground
        }
        // secondary
        if (mouseArea.pressed) return ComponentTokens.buttonSecondaryBackgroundPressed
        if (mouseArea.containsMouse) return ComponentTokens.buttonSecondaryBackgroundHover
        return ComponentTokens.buttonSecondaryBackground
    }

    border.width: root.style === "primary" || root.style === "ghost" ? 0 : 1
    border.color: !root.enabled ? ComponentTokens.buttonDisabledBorder
        : feedbackActive ? DesignSystem.statusSuccess
        : ComponentTokens.buttonSecondaryBorder

    Behavior on color {
        ColorAnimation { duration: SemanticTokens.durationFast }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.feedbackActive ? root.feedbackText : root.text
        color: {
            if (!root.enabled) return ComponentTokens.buttonDisabledText
            if (root.feedbackActive) return "white"
            if (root.style === "primary") return ComponentTokens.buttonPrimaryText
            if (root.style === "ghost") return ComponentTokens.buttonGhostText
            return ComponentTokens.buttonSecondaryText
        }
        font.pixelSize: SemanticTokens.fontSizeBody
        font.family: SemanticTokens.fontFamily
        font.weight: Font.Medium
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: root.enabled
        cursorShape: root.enabled ? CursorTokens.clickable : CursorTokens.defaultCursor
        hoverEnabled: true
        onClicked: root.clicked()
    }

    Timer {
        id: feedbackTimer
        interval: 1500
        onTriggered: root.feedbackActive = false
    }

    function showFeedback() {
        feedbackActive = true
        feedbackTimer.restart()
    }
}
