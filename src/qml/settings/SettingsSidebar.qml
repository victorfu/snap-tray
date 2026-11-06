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

    // Task pages first (#227), then Updates and About after a divider.
    function buildPages() {
        var result = [
            { key: "general", label: qsTr("General"), group: 0 },
            { key: "capture", label: qsTr("Capture"), group: 0 },
            { key: "pins", label: qsTr("Pin Windows"), group: 0 }
        ]
        if (settingsBackend.recordingSupported) {
            result.push({ key: "recording", label: qsTr("Recording"), group: 0 })
        }
        result.push({ key: "saving", label: qsTr("Saving"), group: 0 })
        if (settingsBackend.ocrSettingsVisible) {
            result.push({ key: "ocr", label: qsTr("Text Recognition"), group: 0 })
        }
        result.push({ key: "hotkeys", label: qsTr("Hotkeys"), group: 0 })
        result.push({ key: "updates", label: qsTr("Updates"), group: 1 })
        result.push({ key: "about", label: qsTr("About"), group: 1 })
        return result
    }

    readonly property int itemHeight: 36
    readonly property int dividerSpace: 17

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
            height: root.itemHeight + (startsGroup ? root.dividerSpace : 0)

            required property int index
            required property var modelData

            readonly property string label: modelData.label
            readonly property bool isActive: root.currentIndex === index
            // The first item of a later group draws the divider above itself.
            readonly property bool startsGroup: index > 0 && root.pages[index - 1].group !== modelData.group
            property alias focusArea: hoverArea

            Rectangle {
                objectName: "settingsSidebarDivider"
                visible: navItem.startsGroup
                x: 12
                y: Math.floor((root.dividerSpace - height) / 2)
                width: parent.width - 24
                height: 1
                color: SemanticTokens.borderDefault
            }

            Item {
                id: itemBody
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: root.itemHeight

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
}
