import QtQuick
import SnapTrayQml

/**
 * SettingsPageHeader: Page title and one sentence saying what the page is for (#227).
 *
 * Optional trailing content (for example a page-wide action button) goes in the
 * default slot and sits right of the title.
 */
Item {
    id: root

    property string title: ""
    property string description: ""

    default property alias trailing: trailingRow.data

    width: parent ? parent.width - parent.leftPadding - parent.rightPadding : 0
    implicitHeight: textColumn.implicitHeight + SemanticTokens.spacing8
    height: implicitHeight

    Accessible.role: Accessible.Heading
    Accessible.name: root.title
    Accessible.description: root.description

    Column {
        id: textColumn
        anchors.left: parent.left
        anchors.right: trailingRow.left
        anchors.rightMargin: trailingRow.width > 0 ? SemanticTokens.spacing12 : 0
        spacing: SemanticTokens.spacing4

        Text {
            objectName: "settingsPageTitle"
            width: parent.width
            text: root.title
            color: SemanticTokens.textPrimary
            font.pixelSize: SemanticTokens.fontSizeH3
            font.weight: Font.DemiBold
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            visible: root.description.length > 0
            text: root.description
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeBody
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            wrapMode: Text.WordWrap
        }
    }

    Row {
        id: trailingRow
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: SemanticTokens.spacing8
    }
}
