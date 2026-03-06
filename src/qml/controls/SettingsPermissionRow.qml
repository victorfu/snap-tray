import QtQuick
import SnapTrayQml

/**
 * SettingsPermissionRow: Permission status row with grant action button.
 *
 * Shows a "Granted" / "Not Granted" status label, an optional description
 * explaining why the permission is needed, and an "Open Settings" button.
 */
Column {
    id: root

    property string label: ""
    property string description: ""
    property bool granted: false

    signal openSettingsClicked()

    width: parent ? parent.width - parent.leftPadding - parent.rightPadding : 0
    spacing: 0

    SettingsRow {
        label: root.label
        width: root.width

        Row {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            spacing: SemanticTokens.spacing8

            Text {
                text: root.granted ? qsTr("Granted") : qsTr("Not Granted")
                color: root.granted ? SemanticTokens.statusSuccess : SemanticTokens.statusError
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
                font.letterSpacing: SemanticTokens.letterSpacingDefault
                anchors.verticalCenter: parent.verticalCenter
            }

            SettingsButton {
                text: qsTr("Open Settings")
                onClicked: root.openSettingsClicked()
            }
        }
    }

    // Permission description
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
