import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * AboutSettings: App icon, name, version, copyright, author, website link.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        topPadding: 40
        spacing: SemanticTokens.spacing8

        // App icon
        Image {
            source: "qrc:/icons/icons/snaptray.png"
            sourceSize.width: 64
            sourceSize.height: 64
            width: 64
            height: 64
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Spacer { size: SemanticTokens.spacing4 }

        // App name
        Text {
            text: settingsBackend.appName
            color: SemanticTokens.textPrimary
            font.pixelSize: SemanticTokens.fontSizeH1
            font.weight: Font.Bold
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Version
        Text {
            text: qsTr("Version ") + settingsBackend.appVersion
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Spacer { size: SemanticTokens.spacing12 }

        // Copyright
        Text {
            text: qsTr("Copyright 2026 Victor Fu")
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Author
        Text {
            text: qsTr("Author: Victor Fu")
            color: SemanticTokens.textSecondary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Website link
        Text {
            text: "<a href=\"https://victorfu.github.io/snap-tray/\">https://victorfu.github.io/snap-tray/</a>"
            color: SemanticTokens.accentDefault
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
            linkColor: SemanticTokens.accentDefault
            anchors.horizontalCenter: parent.horizontalCenter
            onLinkActivated: function(link) { Qt.openUrlExternally(link) }

            MouseArea {
                anchors.fill: parent
                cursorShape: parent.hoveredLink ? CursorTokens.clickable : CursorTokens.defaultCursor
                acceptedButtons: Qt.NoButton
            }
        }
    }
}
