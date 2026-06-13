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
    property var pages: buildPages()
    readonly property string currentKey: pages.length > 0
        && currentIndex >= 0
        && currentIndex < pages.length
            ? pages[currentIndex].key
            : "general"

    function buildPages() {
        var result = [
            { key: "general", label: qsTr("General") },
            { key: "hotkeys", label: qsTr("Hotkeys") },
            { key: "advanced", label: qsTr("Advanced") },
            { key: "watermark", label: qsTr("Watermark") }
        ]
        if (settingsBackend.ocrSettingsVisible) {
            result.push({ key: "ocr", label: qsTr("OCR") })
        }
        if (settingsBackend.recordingSupported) {
            result.push({ key: "recording", label: qsTr("Recording") })
        }
        result.push({ key: "files", label: qsTr("Files") })
        result.push({ key: "updates", label: qsTr("Updates") })
        result.push({ key: "about", label: qsTr("About") })
        return result
    }

    color: ComponentTokens.settingsSidebarBg

    ListView {
        id: navList
        anchors.fill: parent
        anchors.topMargin: 12
        interactive: false
        currentIndex: root.currentIndex
        keyNavigationEnabled: true
        activeFocusOnTab: true

        model: root.pages

        delegate: Item {
            id: navItem
            width: navList.width
            height: 36

            required property int index
            required property var modelData

            readonly property string label: modelData.label
            readonly property bool isActive: root.currentIndex === index
            property alias focusArea: hoverArea

            // Active left accent bar
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 20
                radius: 1.5
                color: SemanticTokens.accentDefault
                opacity: navItem.isActive ? 1.0 : 0.0

                Behavior on opacity {
                    NumberAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
                }
            }

            // Background highlight
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 8
                radius: SemanticTokens.radiusSmall
                color: {
                    if (navItem.isActive)
                        return ComponentTokens.settingsSidebarActiveItem
                    if (hoverArea.containsMouse)
                        return ComponentTokens.settingsSidebarHoverItem
                    return "transparent"
                }

                Behavior on color {
                    ColorAnimation { duration: SemanticTokens.durationFast }
                }
            }

            // Label text
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: navItem.label
                color: navItem.isActive
                    ? SemanticTokens.textPrimary
                    : SemanticTokens.textSecondary
                font.pixelSize: SemanticTokens.fontSizeBody
                font.weight: navItem.isActive ? Font.Medium : Font.Normal
                font.family: SemanticTokens.fontFamily
                font.letterSpacing: SemanticTokens.letterSpacingDefault
            }

            FocusFrame {
                showFocus: hoverArea.activeFocus
                anchors.margins: -ComponentTokens.focusRingOffset
                anchors.leftMargin: 4 - ComponentTokens.focusRingOffset
                anchors.rightMargin: 8 - ComponentTokens.focusRingOffset
                anchors.fill: parent
                radius: SemanticTokens.radiusSmall + ComponentTokens.focusRingOffset
            }

            MouseArea {
                id: hoverArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: CursorTokens.clickable
                focus: true

                Accessible.role: Accessible.MenuItem
                Accessible.name: navItem.label

                onClicked: {
                    forceActiveFocus()
                    root.currentIndex = navItem.index
                }
                Keys.onSpacePressed: root.currentIndex = navItem.index
                Keys.onReturnPressed: root.currentIndex = navItem.index
                Keys.onDownPressed: {
                    var nextIndex = navItem.index + 1
                    if (nextIndex < navList.count) {
                        root.currentIndex = nextIndex
                        navList.currentIndex = nextIndex
                        var nextItem = navList.itemAtIndex(nextIndex)
                        if (nextItem) nextItem.focusArea.forceActiveFocus()
                    }
                }
                Keys.onUpPressed: {
                    var prevIndex = navItem.index - 1
                    if (prevIndex >= 0) {
                        root.currentIndex = prevIndex
                        navList.currentIndex = prevIndex
                        var prevItem = navList.itemAtIndex(prevIndex)
                        if (prevItem) prevItem.focusArea.forceActiveFocus()
                    }
                }
            }
        }
    }
}
