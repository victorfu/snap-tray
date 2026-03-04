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
        spacing: 8

        // App icon
        Image {
            source: "qrc:/icons/icons/snaptray.png"
            sourceSize.width: 64
            sourceSize.height: 64
            width: 64
            height: 64
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Item { width: 1; height: 4 }

        // App name
        Text {
            text: settingsBackend.appName
            color: SemanticTokens.textPrimary
            font.pixelSize: PrimitiveTokens.fontSizeH2
            font.weight: Font.Bold
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: -0.2
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Version
        Text {
            text: qsTr("Version ") + settingsBackend.appVersion
            color: SemanticTokens.textSecondary
            font.pixelSize: PrimitiveTokens.fontSizeCaption
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: -0.2
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Item { width: 1; height: 12 }

        // Copyright
        Text {
            text: qsTr("Copyright 2024-2025 Victor Fu")
            color: SemanticTokens.textSecondary
            font.pixelSize: PrimitiveTokens.fontSizeCaption
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: -0.2
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Author
        Text {
            text: qsTr("Author: Victor Fu")
            color: SemanticTokens.textSecondary
            font.pixelSize: PrimitiveTokens.fontSizeCaption
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: -0.2
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Website link
        Text {
            text: "<a href=\"https://victorfu.github.io/snap-tray/\">https://victorfu.github.io/snap-tray/</a>"
            color: SemanticTokens.accentDefault
            font.pixelSize: PrimitiveTokens.fontSizeCaption
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: -0.2
            linkColor: SemanticTokens.accentDefault
            anchors.horizontalCenter: parent.horizontalCenter
            onLinkActivated: function(link) { Qt.openUrlExternally(link) }

            MouseArea {
                anchors.fill: parent
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                acceptedButtons: Qt.NoButton
            }
        }
    }
}
