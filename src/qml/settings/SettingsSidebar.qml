import QtQuick
import SnapTrayQml

/**
 * SettingsSidebar: Linear-style navigation sidebar for settings pages.
 *
 * Displays a vertical list of navigation items with active state highlighting,
 * a left accent bar on the active item, and hover effects.
 */
Rectangle {
    id: root

    property int currentIndex: 0

    color: ComponentTokens.settingsSidebarBg

    ListView {
        id: navList
        anchors.fill: parent
        anchors.topMargin: 12
        interactive: false
        currentIndex: root.currentIndex

        model: [
            qsTr("General"),
            qsTr("Hotkeys"),
            qsTr("Advanced"),
            qsTr("Watermark"),
            qsTr("OCR"),
            qsTr("Recording"),
            qsTr("Files"),
            qsTr("Updates"),
            qsTr("About")
        ]

        delegate: Item {
            id: navItem
            width: navList.width
            height: 36

            required property int index
            required property string modelData

            readonly property bool isActive: root.currentIndex === index

            // Active left accent bar
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 20
                radius: 1.5
                color: SemanticTokens.accentDefault
                visible: navItem.isActive
            }

            // Background highlight
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 8
                radius: PrimitiveTokens.radiusSmall
                color: {
                    if (navItem.isActive)
                        return ComponentTokens.settingsSidebarActiveItem
                    if (hoverArea.containsMouse)
                        return ComponentTokens.settingsSidebarHoverItem
                    return "transparent"
                }
            }

            // Label text
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: navItem.modelData
                color: navItem.isActive
                    ? SemanticTokens.textPrimary
                    : SemanticTokens.textSecondary
                font.pixelSize: PrimitiveTokens.fontSizeBody
                font.weight: navItem.isActive ? Font.Medium : Font.Normal
                font.family: PrimitiveTokens.fontFamily
                font.letterSpacing: -0.2
            }

            MouseArea {
                id: hoverArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.currentIndex = navItem.index
            }
        }
    }
}
