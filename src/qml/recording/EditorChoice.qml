import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

// Label stays visible when the selected value changes; every choice is discoverable.
Column {
    id: root
    property string label: ""
    property string tooltipText: ""
    property var choices: []
    property int currentIndex: 0
    // Imported projects may contain valid values between the presets.
    property string currentValueText: ""
    readonly property bool hasCustomValue: currentIndex < 0 && currentValueText.length > 0
    signal activated(int index)
    width: parent ? parent.width : 240
    spacing: 4
    opacity: enabled ? 1 : 0.6
    Text {
        text: root.label
        width: parent.width
        wrapMode: Text.WordWrap
        color: SemanticTokens.textSecondary
        font.pixelSize: SemanticTokens.fontSizeCaption
    }
    ComboBox {
        id: control
        objectName: "editorChoiceControl"
        width: parent.width
        model: root.hasCustomValue ? root.choices.concat([root.currentValueText]) : root.choices
        currentIndex: root.hasCustomValue ? root.choices.length : root.currentIndex
        Accessible.name: root.label
        Accessible.description: root.tooltipText
        onActivated: function(index) {
            if (index < root.choices.length) root.activated(index)
        }
        font.pixelSize: SemanticTokens.fontSizeBody
        palette.text: SemanticTokens.textPrimary
        palette.buttonText: SemanticTokens.textPrimary
        palette.base: ComponentTokens.panelBackground
        palette.button: ComponentTokens.inputBackground
        palette.highlight: SemanticTokens.accentDefault
        background: Rectangle {
            implicitHeight: 32
            radius: SemanticTokens.radiusSmall
            color: control.down ? ComponentTokens.recordingPreviewPanelPressed : ComponentTokens.inputBackground
            border.color: control.activeFocus ? SemanticTokens.accentDefault : ComponentTokens.inputBorder
        }
        ToolTip.visible: hovered && root.tooltipText.length > 0
        ToolTip.text: root.tooltipText
    }
}
