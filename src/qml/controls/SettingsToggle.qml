import QtQuick
import SnapTrayQml

/**
 * SettingsToggle: Toggle switch row for boolean settings.
 *
 * Displays a label with a custom animated toggle switch. Includes optional
 * description text below the row.
 */
Column {
    id: root

    property string label: ""
    property string description: ""
    property bool checked: false

    signal toggled(bool checked)

    width: parent ? parent.width - parent.leftPadding - parent.rightPadding : 0
    spacing: 0

    SettingsRow {
        label: root.label
        width: root.width

        Item {
            width: parent.width
            height: parent.height

            // Toggle track
            Rectangle {
                id: track
                anchors.verticalCenter: parent.verticalCenter
                width: ComponentTokens.toggleWidth
                height: ComponentTokens.toggleHeight
                radius: ComponentTokens.toggleHeight / 2
                color: root.checked ? ComponentTokens.toggleTrackOn : ComponentTokens.toggleTrackOff

                Behavior on color {
                    ColorAnimation { duration: SemanticTokens.durationFast }
                }

                // Toggle knob
                Rectangle {
                    id: knob
                    width: ComponentTokens.toggleKnobSize
                    height: ComponentTokens.toggleKnobSize
                    radius: ComponentTokens.toggleKnobRadius
                    color: ComponentTokens.toggleKnob
                    anchors.verticalCenter: parent.verticalCenter
                    x: root.checked ? (track.width - knob.width - ComponentTokens.toggleKnobInset) : ComponentTokens.toggleKnobInset

                    Behavior on x {
                        NumberAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
                    }
                }

                FocusFrame {
                    showFocus: toggleArea.activeFocus
                    extraRadius: track.radius
                }
            }

            MouseArea {
                id: toggleArea
                anchors.fill: parent
                cursorShape: CursorTokens.clickable
                focus: true

                Accessible.role: Accessible.CheckBox
                Accessible.name: root.label
                Accessible.checked: root.checked

                onClicked: {
                    forceActiveFocus()
                    root.checked = !root.checked
                    root.toggled(root.checked)
                }
                Keys.onSpacePressed: {
                    root.checked = !root.checked
                    root.toggled(root.checked)
                }
            }
        }
    }

    // Optional description
    Text {
        text: root.description
        color: SemanticTokens.textTertiary
        font.pixelSize: SemanticTokens.fontSizeCaption
        font.family: SemanticTokens.fontFamily
        font.letterSpacing: SemanticTokens.letterSpacingDefault
        visible: root.description.length > 0
        leftPadding: ComponentTokens.toggleLabelIndent
        topPadding: 2
        width: root.width
        wrapMode: Text.WordWrap
    }
}
