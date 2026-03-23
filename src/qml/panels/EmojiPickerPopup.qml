import QtQuick
import SnapTrayQml

Item {
    id: root

    readonly property var backend: typeof emojiPickerBackend !== "undefined" ? emojiPickerBackend : null
    readonly property string emojiFontFamily: Qt.platform.os === "osx"
        ? "Apple Color Emoji"
        : (Qt.platform.os === "windows" ? "Segoe UI Emoji" : "Noto Color Emoji")

    width: 236
    height: 64

    GlassSurface {
        anchors.fill: parent
        glassRadius: 6
    }

    Grid {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        rows: 2
        columns: 8
        rowSpacing: 4
        columnSpacing: 4

        Repeater {
            model: root.backend ? root.backend.emojiModel : []

            delegate: Rectangle {
                width: 24
                height: 24
                radius: 4
                color: emojiMouse.pressed
                    ? ComponentTokens.emojiCellPressed
                    : emojiMouse.containsMouse
                        ? ComponentTokens.emojiCellHover
                        : "transparent"

                border.width: root.backend && root.backend.currentEmoji === modelData ? 2 : 0
                border.color: ComponentTokens.emojiCellSelectedRing

                Behavior on color {
                    ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
                }

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.family: root.emojiFontFamily
                    font.pointSize: 14
                }

                MouseArea {
                    id: emojiMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: CursorTokens.clickable

                    onClicked: {
                        if (root.backend)
                            root.backend.activateEmoji(modelData)
                    }
                }
            }
        }
    }
}
