import QtQuick
import SnapTrayQml

/**
 * SettingsRow: Generic label + content row for settings forms.
 *
 * Places a fixed-width label on the left with a default content slot on the right.
 */
Row {
    id: root

    property string label: ""
    property int labelWidth: 140

    default property alias content: contentContainer.data

    width: parent ? parent.width - parent.leftPadding - parent.rightPadding : 0
    height: 36
    spacing: 0

    Text {
        text: root.label
        color: SemanticTokens.textPrimary
        font.pixelSize: SemanticTokens.fontSizeBody
        font.family: SemanticTokens.fontFamily
        font.letterSpacing: SemanticTokens.letterSpacingDefault
        width: root.labelWidth
        anchors.verticalCenter: parent.verticalCenter
    }

    Item {
        id: contentContainer
        width: root.width - root.labelWidth
        height: parent.height
    }
}
