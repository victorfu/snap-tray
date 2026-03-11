import QtQuick
import SnapTrayQml

Item {
    id: root

    property string title: ""
    property string subtitle: ""
    property string iconText: ""
    property Component contentItem: null
    property Component buttonBar: null
    property int dialogWidth: 420

    signal closeRequested()

    width: dialogWidth
    height: titleBar.height + contentLoader.height + buttonBarLoader.height

    focus: true
    Keys.onEscapePressed: root.closeRequested()

    // Glass background
    GlassSurface {
        anchors.fill: parent
        glassBg: ComponentTokens.toolbarPopupBackground
        glassBgTop: ComponentTokens.toolbarPopupBackgroundTop
        glassHighlight: ComponentTokens.toolbarPopupHighlight
        glassBorder: ComponentTokens.toolbarPopupBorder
        glassRadius: DesignSystem.dialogRadius
    }

    Column {
        anchors.fill: parent

        // Title bar (72px, draggable)
        Item {
            id: titleBar
            width: parent.width
            height: 72

            MouseArea {
                id: dragArea
                anchors.fill: parent
                property point screenDragStart
                property point windowStart
                cursorShape: Qt.SizeAllCursor

                onPressed: function(mouse) {
                    let global = mapToGlobal(mouse.x, mouse.y)
                    screenDragStart = Qt.point(global.x, global.y)
                    let win = root.Window.window
                    if (win) windowStart = Qt.point(win.x, win.y)
                }
                onPositionChanged: function(mouse) {
                    let win = root.Window.window
                    if (win && pressed) {
                        let global = mapToGlobal(mouse.x, mouse.y)
                        win.x = windowStart.x + (global.x - screenDragStart.x)
                        win.y = windowStart.y + (global.y - screenDragStart.y)
                    }
                }
            }

            Row {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12

                // Icon
                Rectangle {
                    width: 56; height: 56
                    radius: DesignSystem.radiusMedium
                    color: DesignSystem.inputBackground
                    border.width: 1
                    border.color: DesignSystem.inputBorder

                    Text {
                        anchors.centerIn: parent
                        text: root.iconText
                        color: SemanticTokens.textSecondary
                        font.pixelSize: 16
                        font.family: SemanticTokens.fontFamily
                        font.weight: Font.Medium
                    }
                }

                // Title + subtitle
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Text {
                        text: root.title
                        color: SemanticTokens.textPrimary
                        font.pixelSize: DesignSystem.fontSizeH3
                        font.family: SemanticTokens.fontFamily
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: root.subtitle
                        color: SemanticTokens.textSecondary
                        font.pixelSize: DesignSystem.fontSizeSmallBody
                        font.family: SemanticTokens.fontFamily
                        visible: text.length > 0
                    }
                }
            }
        }

        // Content area
        Loader {
            id: contentLoader
            width: parent.width
            sourceComponent: root.contentItem
        }

        // Button bar (56px)
        Loader {
            id: buttonBarLoader
            width: parent.width
            sourceComponent: root.buttonBar
        }
    }
}
