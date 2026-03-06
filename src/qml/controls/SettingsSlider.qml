import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * SettingsSlider: Labeled slider with value display for numeric settings.
 *
 * Custom-styled slider using token colors. Displays current value with
 * optional suffix (e.g. "%").
 */
SettingsRow {
    id: root

    property int value: 0
    property int from: 0
    property int to: 100
    property string suffix: ""
    property int stepSize: 1

    signal moved(int value)

    Slider {
        id: slider
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - valueText.width - 8
        from: root.from
        to: root.to
        value: root.value
        stepSize: root.stepSize
        live: true

        Accessible.role: Accessible.Slider
        Accessible.name: root.label

        onMoved: {
            root.value = Math.round(slider.value)
            root.moved(root.value)
        }

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: ComponentTokens.sliderHeight
            radius: ComponentTokens.sliderHeight / 2
            color: ComponentTokens.sliderTrack

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: ComponentTokens.sliderFill

                Behavior on width {
                    enabled: !slider.pressed
                    NumberAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
                }
            }
        }

        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth) - width / 2
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: ComponentTokens.sliderKnobSize
            height: ComponentTokens.sliderKnobSize
            radius: ComponentTokens.sliderKnobSize / 2
            color: ComponentTokens.sliderKnob
            border.width: 1
            border.color: SemanticTokens.borderDefault

            FocusFrame {
                showFocus: slider.activeFocus
                extraRadius: ComponentTokens.sliderKnobSize / 2
            }
        }
    }

    Text {
        id: valueText
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        width: 50
        horizontalAlignment: Text.AlignRight
        text: root.value + root.suffix
        color: SemanticTokens.textSecondary
        font.pixelSize: SemanticTokens.fontSizeBody
        font.family: SemanticTokens.fontFamily
        font.letterSpacing: SemanticTokens.letterSpacingDefault
    }
}
