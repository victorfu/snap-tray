import QtQuick
import SnapTrayQml

/**
 * ScrollCaptureControlBar: Floating control bar for scroll capture.
 *
 * Always-dark surface (theme-independent) rendered over arbitrary screen
 * content. Provides Done (brand purple) and Cancel (subtle glass) buttons
 * with smooth hover/press animations. Draggable by non-button areas.
 *
 * C++ bridge sets slowScrollWarning; reads signals escapePressed,
 * finishRequested, cancelRequested.
 */
Item {
    id: root

    // ---- Properties set from C++ ----
    property bool slowScrollWarning: false

    // ---- Signals ----
    signal escapePressed()
    signal finishRequested()
    signal cancelRequested()

    // ---- Escape shortcut ----
    Shortcut {
        sequence: "Escape"
        onActivated: root.escapePressed()
    }

    // ---- Drag state ----
    property bool dragging: false
    property point dragStart: Qt.point(0, 0)
    property point windowStart: Qt.point(0, 0)

    // ---- Sizing ----
    implicitWidth: panel.implicitWidth
    implicitHeight: panel.implicitHeight

    // ---- Panel background (dark glass) ----
    Rectangle {
        id: panel
        anchors.fill: parent
        implicitWidth: buttonRow.width + ComponentTokens.scrollControlPadding * 2
        implicitHeight: buttonRow.height + ComponentTokens.scrollControlPadding * 2
        radius: PrimitiveTokens.radiusLarge
        color: ComponentTokens.scrollControlBackground
        border.width: 1
        border.color: ComponentTokens.scrollControlBorder

        Row {
            id: buttonRow
            spacing: ComponentTokens.scrollControlSpacing
            anchors.centerIn: parent

            // ---- Done button (brand purple) ----
            Rectangle {
                id: doneButton
                width: 84
                height: 34
                radius: ComponentTokens.scrollControlDoneRadius
                color: doneArea.pressed
                    ? ComponentTokens.scrollControlDoneBackgroundPressed
                    : doneArea.containsMouse
                        ? ComponentTokens.scrollControlDoneBackgroundHover
                        : ComponentTokens.scrollControlDoneBackground

                Behavior on color {
                    ColorAnimation { duration: PrimitiveTokens.durationFast }
                }

                Accessible.role: Accessible.Button
                Accessible.name: doneText.text

                // Warning ring for slow scroll
                Rectangle {
                    id: warningRing
                    visible: root.slowScrollWarning
                    anchors.fill: parent
                    anchors.margins: -4
                    radius: parent.radius + 4
                    color: "transparent"
                    border.width: 2
                    border.color: ComponentTokens.scrollControlWarningBorder

                    SequentialAnimation on opacity {
                        running: root.slowScrollWarning
                        loops: Animation.Infinite
                        NumberAnimation {
                            to: 1.0
                            duration: 400
                            easing.type: Easing.InOutSine
                        }
                        NumberAnimation {
                            to: 0.3
                            duration: 400
                            easing.type: Easing.InOutSine
                        }
                    }
                }

                Text {
                    id: doneText
                    anchors.centerIn: parent
                    text: qsTr("Done")
                    color: ComponentTokens.scrollControlDoneText
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.weight: PrimitiveTokens.fontWeightBold
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
                }

                MouseArea {
                    id: doneArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.finishRequested()
                }
            }

            // ---- Cancel button (subtle glass) ----
            Rectangle {
                id: cancelButton
                width: 84
                height: 34
                radius: ComponentTokens.scrollControlCancelRadius
                color: cancelArea.pressed
                    ? ComponentTokens.scrollControlCancelBackgroundPressed
                    : cancelArea.containsMouse
                        ? ComponentTokens.scrollControlCancelBackgroundHover
                        : ComponentTokens.scrollControlCancelBackground
                border.width: 1
                border.color: ComponentTokens.scrollControlCancelBorder

                Behavior on color {
                    ColorAnimation { duration: PrimitiveTokens.durationFast }
                }

                Accessible.role: Accessible.Button
                Accessible.name: cancelText.text

                Text {
                    id: cancelText
                    anchors.centerIn: parent
                    text: qsTr("Cancel")
                    color: ComponentTokens.scrollControlCancelText
                    font.pixelSize: PrimitiveTokens.fontSizeBody
                    font.weight: PrimitiveTokens.fontWeightSemiBold
                    font.family: PrimitiveTokens.fontFamily
                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
                }

                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.cancelRequested()
                }
            }
        }

        // ---- Drag handler for non-button areas ----
        MouseArea {
            id: dragArea
            anchors.fill: parent
            z: -1
            cursorShape: root.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

            onPressed: function(mouse) {
                root.dragging = true;
                root.dragStart = Qt.point(mouse.x, mouse.y);
                if (root.Window.window) {
                    root.windowStart = Qt.point(
                        root.Window.window.x,
                        root.Window.window.y
                    );
                }
            }

            onPositionChanged: function(mouse) {
                if (!root.dragging) return;
                var w = root.Window.window;
                if (!w) return;
                var dx = mouse.x - root.dragStart.x;
                var dy = mouse.y - root.dragStart.y;
                w.x = root.windowStart.x + dx;
                w.y = root.windowStart.y + dy;
            }

            onReleased: {
                root.dragging = false;
            }
        }
    }
}
