import QtQuick
import SnapTrayQml

/**
 * SettingsPathPicker: Path display with Browse button for file/directory selection.
 *
 * Displays a path (or placeholder when empty) with a Browse button on the right.
 */
SettingsRow {
    id: root

    property string path: ""
    property string placeholder: ""

    signal browseClicked()

    Row {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        spacing: SemanticTokens.spacing8

        // Path display
        Rectangle {
            width: parent.width - browseBtn.width - parent.spacing
            height: ComponentTokens.inputHeight
            radius: ComponentTokens.inputRadius
            color: ComponentTokens.inputBackground
            border.width: 1
            border.color: ComponentTokens.inputBorder
            anchors.verticalCenter: parent.verticalCenter

            Text {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                verticalAlignment: Text.AlignVCenter
                text: root.path.length > 0 ? root.path : root.placeholder
                color: root.path.length > 0 ? SemanticTokens.textPrimary : SemanticTokens.textTertiary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
                font.letterSpacing: SemanticTokens.letterSpacingDefault
                elide: Text.ElideMiddle
            }
        }

        SettingsButton {
            id: browseBtn
            text: qsTr("Browse...")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.browseClicked()
        }
    }
}
