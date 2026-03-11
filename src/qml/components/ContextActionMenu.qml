import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * ContextActionMenu: Token-styled popup action menu with optional Lucide icons.
 */
Popup {
    id: root

    property var model: []
    property int menuWidth: 190

    signal actionTriggered(string actionId)

    padding: 6
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside | Popup.CloseOnReleaseOutside

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: SemanticTokens.durationFast; easing.type: Easing.OutCubic }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: SemanticTokens.durationFast; easing.type: Easing.InCubic }
    }

    background: Rectangle {
        radius: ComponentTokens.panelRadius
        color: ComponentTokens.panelGlassBackground
        border.width: 1
        border.color: ComponentTokens.panelGlassBorder
    }

    contentItem: Column {
        spacing: 2

        Repeater {
            model: root.model

            delegate: Item {
                width: root.menuWidth
                height: modelData.separator ? 9 : 34

                Rectangle {
                    visible: modelData.separator === true
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 1
                    color: SemanticTokens.borderDefault
                    opacity: 0.5
                }

                Rectangle {
                    visible: modelData.separator !== true
                    anchors.fill: parent
                    radius: SemanticTokens.radiusSmall
                    color: mouseArea.pressed
                        ? ComponentTokens.thumbnailCardSelected
                        : mouseArea.containsMouse
                            ? ComponentTokens.thumbnailCardHover
                            : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: SemanticTokens.durationFast; easing.type: Easing.InOutCubic }
                    }
                }

                SvgIcon {
                    visible: modelData.separator !== true && modelData.icon !== undefined && modelData.icon !== ""
                    anchors.left: parent.left
                    anchors.leftMargin: SemanticTokens.spacing8
                    anchors.verticalCenter: parent.verticalCenter
                    source: modelData.icon || ""
                    iconSize: ComponentTokens.iconSizeMenu
                    color: SemanticTokens.iconColor
                }

                Text {
                    visible: modelData.separator !== true
                    anchors.left: parent.left
                    anchors.leftMargin: modelData.icon !== undefined && modelData.icon !== ""
                        ? 32
                        : SemanticTokens.spacing8
                    anchors.right: parent.right
                    anchors.rightMargin: SemanticTokens.spacing8
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.text || ""
                    color: SemanticTokens.textPrimary
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    font.weight: SemanticTokens.fontWeightMedium
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: mouseArea
                    visible: modelData.separator !== true
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor

                    onClicked: function(mouse) {
                        mouse.accepted = true
                        root.close()
                        root.actionTriggered(modelData.id || "")
                    }
                }
            }
        }
    }
}
