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
                    ColorAnimation { duration: PrimitiveTokens.durationFast }
                }

                // Toggle knob
                Rectangle {
                    id: knob
                    width: 16
                    height: 16
                    radius: 8
                    color: ComponentTokens.toggleKnob
                    anchors.verticalCenter: parent.verticalCenter
                    x: root.checked ? (track.width - knob.width - 2) : 2

                    Behavior on x {
                        NumberAnimation { duration: PrimitiveTokens.durationFast; easing.type: Easing.InOutCubic }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
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
        font.pixelSize: PrimitiveTokens.fontSizeCaption
        font.family: PrimitiveTokens.fontFamily
        font.letterSpacing: -0.2
        visible: root.description.length > 0
        leftPadding: 140
        topPadding: 2
        width: root.width
        wrapMode: Text.WordWrap
    }
}
