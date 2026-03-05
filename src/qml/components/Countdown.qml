import QtQuick
import SnapTrayQml

/**
 * Countdown.qml
 *
 * Full-screen countdown overlay displayed before recording starts.
 * Shows a 3-2-1 countdown with scale animation effect.
 *
 * The C++ bridge sets `countdownSeconds` and calls start()/cancel().
 */
Item {
    id: root

    // Countdown duration (set from C++ before calling start())
    property int countdownSeconds: 3

    // Internal state
    property int currentSecond: countdownSeconds
    property bool running: false

    // Animation properties (reset each tick)
    property real numberScale: 1.5
    property real numberOpacity: 0.0

    signal finished()
    signal cancelled()

    function start() {
        currentSecond = countdownSeconds;
        running = true;
        triggerTickAnimation();
        tickTimer.start();
    }

    function cancel() {
        if (!running) return;
        running = false;
        tickTimer.stop();
        tickAnimation.stop();
        cancelled();
    }

    function triggerTickAnimation() {
        // Reset to start values and animate
        numberScale = 1.5;
        numberOpacity = 0.0;
        tickAnimation.start();
    }

    // Semi-transparent dark background
    Rectangle {
        anchors.fill: parent
        color: ComponentTokens.countdownOverlay
    }

    // Countdown number
    Text {
        id: countdownText
        anchors.centerIn: parent
        text: root.currentSecond
        font.pixelSize: ComponentTokens.countdownFontSize
        font.weight: Font.Bold
        color: Qt.rgba(ComponentTokens.countdownText.r, ComponentTokens.countdownText.g, ComponentTokens.countdownText.b, root.numberOpacity)
        scale: root.numberScale
    }

    // Scale + opacity animation per tick
    ParallelAnimation {
        id: tickAnimation

        NumberAnimation {
            target: root
            property: "numberScale"
            from: 1.5
            to: 1.0
            duration: ComponentTokens.countdownScaleDuration
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: root
            property: "numberOpacity"
            from: 0.0
            to: 1.0
            duration: ComponentTokens.countdownFadeDuration
            easing.type: Easing.OutQuad
        }
    }

    // 1-second tick timer
    Timer {
        id: tickTimer
        interval: 1000
        repeat: true
        onTriggered: {
            root.currentSecond--;
            if (root.currentSecond <= 0) {
                tickTimer.stop();
                tickAnimation.stop();
                root.running = false;
                root.finished();
            } else {
                root.triggerTickAnimation();
            }
        }
    }

    // Escape to cancel
    focus: true
    Keys.onEscapePressed: function(event) {
        cancel();
        event.accepted = true;
    }
}
