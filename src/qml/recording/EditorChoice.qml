import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

// A labelled choice. Up to four alternatives are shown side by side as a
// segmented selector so every value is visible (#221 §8); longer lists use a
// compact drop-down. The label stays visible when the value changes.
Item {
    id: root
    property string label: ""
    property string tooltipText: ""
    property var choices: []
    property int currentIndex: 0
    // Imported projects may contain valid values between the presets.
    property string currentValueText: ""
    // The label sits beside the control instead of above it (toolbars).
    property bool inlineLabel: false
    readonly property bool hasCustomValue: currentIndex < 0 && currentValueText.length > 0
    readonly property bool segmented: choices.length > 0 && choices.length <= maxSegments
    readonly property int maxSegments: 4
    readonly property int controlHeight: 32
    // Inline: a long translated label elides before the control gets too small.
    readonly property int minimumControlWidth: 88
    // Wider where there is room (a long translation then shows in full).
    property int maximumInlineLabelWidth: 140
    signal activated(int index)

    implicitWidth: inlineLabel ? Math.min(labelText.implicitWidth, maximumInlineLabelWidth) + 8 + minimumControlWidth : 240
    width: parent ? parent.width : 240
    implicitHeight: inlineLabel ? controlHeight : labelText.height + 4 + controlHeight + (customNote.visible ? customNote.height + 4 : 0)
    height: implicitHeight
    opacity: enabled ? 1 : 0.6

    Text {
        id: labelText
        text: root.label
        visible: text.length > 0
        width: root.inlineLabel ? Math.min(implicitWidth, root.maximumInlineLabelWidth) : parent.width
        elide: root.inlineLabel ? Text.ElideRight : Text.ElideNone
        height: root.inlineLabel ? root.controlHeight : implicitHeight
        verticalAlignment: Text.AlignVCenter
        wrapMode: root.inlineLabel ? Text.NoWrap : Text.WordWrap
        color: SemanticTokens.textSecondary
        font.pixelSize: SemanticTokens.fontSizeCaption
    }

    Loader {
        id: controlLoader
        x: root.inlineLabel && labelText.visible ? labelText.width + 8 : 0
        y: root.inlineLabel ? 0 : labelText.height + 4
        width: parent.width - x
        height: root.controlHeight
        sourceComponent: root.segmented ? segmentedComponent : comboComponent
    }

    // A value between the presets is reported, never offered as a choice.
    Text {
        id: customNote
        visible: root.segmented && root.hasCustomValue && !root.inlineLabel
        y: controlLoader.y + controlLoader.height + 4
        width: parent.width
        text: qsTr("Current: %1").arg(root.currentValueText)
        color: SemanticTokens.textSecondary
        font.pixelSize: SemanticTokens.fontSizeCaption
        elide: Text.ElideRight
    }

    Component {
        id: segmentedComponent
        Rectangle {
            id: track
            objectName: "editorChoiceControl"
            // The same surface the drop-down offers, so callers and tests drive either one.
            readonly property int currentIndex: root.hasCustomValue ? root.choices.length : root.currentIndex
            readonly property string currentText: root.hasCustomValue ? root.currentValueText
                : root.currentIndex >= 0 && root.currentIndex < root.choices.length ? String(root.choices[root.currentIndex]) : ""
            signal activated(int index)
            onActivated: function(index) {
                if (index >= 0 && index < root.choices.length) root.activated(index)
            }
            radius: SemanticTokens.radiusSmall
            color: ComponentTokens.editorControl
            border.width: 1
            border.color: activeFocus ? SemanticTokens.accentDefault : ComponentTokens.editorBorder
            activeFocusOnTab: root.enabled
            Accessible.role: Accessible.Grouping
            Accessible.name: root.label
            Accessible.description: root.tooltipText
            // Arrows move between presets; from a custom value they reach both ends.
            Keys.onLeftPressed: {
                if (root.hasCustomValue) activated(root.choices.length - 1)
                else if (currentIndex > 0) activated(currentIndex - 1)
            }
            Keys.onRightPressed: {
                if (root.hasCustomValue) activated(0)
                else if (currentIndex + 1 < root.choices.length) activated(currentIndex + 1)
            }
            // Space and Return stay here: they never reach playback or Export.
            Keys.onSpacePressed: function(event) { event.accepted = true }
            Keys.onReturnPressed: function(event) { event.accepted = true }
            Keys.onEnterPressed: function(event) { event.accepted = true }
            FocusFrame { anchors.fill: parent; showFocus: track.activeFocus }

            Row {
                anchors.fill: parent
                anchors.margins: 2
                Repeater {
                    model: root.choices
                    Rectangle {
                        id: segment
                        required property int index
                        required property var modelData
                        readonly property bool chosen: !root.hasCustomValue && index === root.currentIndex
                        width: (track.width - 4) / Math.max(1, root.choices.length)
                        height: track.height - 4
                        radius: SemanticTokens.radiusSmall - 1
                        color: chosen ? SemanticTokens.accentDefault
                             : segmentMouse.containsMouse && root.enabled ? ComponentTokens.editorControlHover : "transparent"
                        Accessible.role: Accessible.RadioButton
                        Accessible.checked: chosen
                        Accessible.name: String(modelData)
                        Accessible.onPressAction: track.activated(index)
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 4
                            anchors.rightMargin: 4
                            text: String(segment.modelData)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            color: segment.chosen ? SemanticTokens.textOnAccent : SemanticTokens.textPrimary
                            font.pixelSize: SemanticTokens.fontSizeSmall
                            font.weight: segment.chosen ? SemanticTokens.fontWeightSemiBold : SemanticTokens.fontWeightRegular
                        }
                        MouseArea {
                            id: segmentMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: root.enabled
                            cursorShape: CursorTokens.clickable
                            onClicked: track.activated(segment.index)
                        }
                    }
                }
            }
            ToolTip.visible: root.tooltipText.length > 0 && trackHover.hovered
            ToolTip.text: root.tooltipText
            ToolTip.delay: 500
            HoverHandler { id: trackHover }
        }
    }

    Component {
        id: comboComponent
        ComboBox {
            id: control
            objectName: "editorChoiceControl"
            model: root.hasCustomValue ? root.choices.concat([root.currentValueText]) : root.choices
            currentIndex: root.hasCustomValue ? root.choices.length : root.currentIndex
            // Label and current choice (#238); the shown text is a custom contentItem.
            Accessible.name: displayText !== "" ? root.label + ", " + displayText : root.label
            Accessible.description: root.tooltipText
            onActivated: function(index) {
                if (index < root.choices.length) root.activated(index)
            }
            font.pixelSize: SemanticTokens.fontSizeSmall
            palette.text: SemanticTokens.textPrimary
            palette.buttonText: SemanticTokens.textPrimary
            palette.base: ComponentTokens.editorSurface
            palette.window: ComponentTokens.editorSurface
            palette.button: ComponentTokens.editorControl
            palette.highlight: SemanticTokens.accentDefault
            palette.highlightedText: SemanticTokens.textOnAccent
            leftPadding: 10
            rightPadding: 28
            contentItem: Text {
                text: control.displayText
                color: SemanticTokens.textPrimary
                font: control.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            indicator: SvgIcon {
                x: control.width - width - 8
                y: (control.height - height) / 2
                iconSize: 14
                source: "qrc:/icons/icons/chevron-down.svg"
                color: SemanticTokens.textSecondary
            }
            background: Rectangle {
                implicitHeight: root.controlHeight
                radius: SemanticTokens.radiusSmall
                color: control.down || control.hovered ? ComponentTokens.editorControlHover : ComponentTokens.editorControl
                border.color: control.activeFocus ? SemanticTokens.accentDefault : ComponentTokens.editorBorder
            }
            ToolTip.visible: hovered && root.tooltipText.length > 0
            ToolTip.text: root.tooltipText
            ToolTip.delay: 500
        }
    }
}
