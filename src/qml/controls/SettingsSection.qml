import QtQuick
import SnapTrayQml

/**
 * SettingsSection: Section header divider for settings pages.
 *
 * Displays an uppercase caption title with optional description text.
 * Uses token system for all colors and sizes.
 */
Column {
    id: root

    property string title: ""
    property string description: ""

    width: parent ? parent.width - parent.leftPadding - parent.rightPadding : 0
    topPadding: ComponentTokens.settingsSectionTopPadding
    spacing: 2

    Text {
        text: root.title.toUpperCase()
        color: ComponentTokens.settingsSectionText
        font.pixelSize: SemanticTokens.fontSizeCaption
        font.weight: Font.Medium
        font.family: SemanticTokens.fontFamily
        font.letterSpacing: SemanticTokens.letterSpacingWide
    }

    Text {
        text: root.description
        color: SemanticTokens.textTertiary
        font.pixelSize: SemanticTokens.fontSizeCaption
        font.family: SemanticTokens.fontFamily
        font.letterSpacing: SemanticTokens.letterSpacingDefault
        visible: root.description.length > 0
    }

    Spacer {}
}
