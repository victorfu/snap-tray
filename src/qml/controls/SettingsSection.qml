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
    topPadding: 16
    spacing: 2

    Text {
        text: root.title.toUpperCase()
        color: ComponentTokens.settingsSectionText
        font.pixelSize: PrimitiveTokens.fontSizeCaption
        font.weight: Font.Medium
        font.family: PrimitiveTokens.fontFamily
        font.letterSpacing: 0.5
    }

    Text {
        text: root.description
        color: SemanticTokens.textTertiary
        font.pixelSize: PrimitiveTokens.fontSizeCaption
        font.family: PrimitiveTokens.fontFamily
        font.letterSpacing: -0.2
        visible: root.description.length > 0
    }

    Item { width: 1; height: 8 }
}
