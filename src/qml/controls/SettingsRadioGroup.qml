import QtQuick
import SnapTrayQml

/**
 * SettingsRadioGroup: Reusable radio button group for settings.
 *
 * Accepts a model of {text, value} objects. Supports keyboard navigation
 * (up/down arrows) and screen reader accessibility.
 */
Rectangle {
    id: root

    property var model: []
    property var currentValue: undefined

    signal activated(var value)

    width: parent ? parent.width - parent.leftPadding - parent.rightPadding : 0
    height: groupColumn.height + 2 * SemanticTokens.spacing8
    radius: SemanticTokens.radiusSmall
    color: ComponentTokens.inputBackground
    border.width: 1
    border.color: ComponentTokens.inputBorder

    Accessible.role: Accessible.Grouping

    Column {
        id: groupColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: SemanticTokens.spacing8
        spacing: SemanticTokens.spacing4

        Repeater {
            id: repeater
            model: root.model

            delegate: Rectangle {
                id: optionItem
                width: groupColumn.width
                height: 32
                radius: SemanticTokens.radiusSmall

                required property int index
                required property var modelData

                readonly property bool isSelected: root.currentValue === modelData.value
                property alias focusArea: optionArea

                color: isSelected ? ComponentTokens.settingsSidebarActiveItem : "transparent"

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: SemanticTokens.spacing8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: SemanticTokens.spacing8

                    // Radio indicator
                    Rectangle {
                        width: 16
                        height: 16
                        radius: 8
                        border.width: 2
                        border.color: optionItem.isSelected
                            ? SemanticTokens.accentDefault
                            : SemanticTokens.textTertiary
                        color: "transparent"
                        anchors.verticalCenter: parent.verticalCenter

                        Rectangle {
                            anchors.centerIn: parent
                            width: 8
                            height: 8
                            radius: 4
                            color: SemanticTokens.accentDefault
                            visible: optionItem.isSelected
                        }
                    }

                    Text {
                        text: optionItem.modelData.text
                        color: SemanticTokens.textPrimary
                        font.pixelSize: SemanticTokens.fontSizeBody
                        font.family: SemanticTokens.fontFamily
                        font.letterSpacing: SemanticTokens.letterSpacingDefault
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                FocusFrame {
                    showFocus: optionArea.activeFocus
                    extraRadius: optionItem.radius
                }

                MouseArea {
                    id: optionArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    focus: true

                    Accessible.role: Accessible.RadioButton
                    Accessible.name: optionItem.modelData.text
                    Accessible.checked: optionItem.isSelected

                    onClicked: {
                        forceActiveFocus()
                        root.currentValue = optionItem.modelData.value
                        root.activated(optionItem.modelData.value)
                    }
                    Keys.onSpacePressed: {
                        root.currentValue = optionItem.modelData.value
                        root.activated(optionItem.modelData.value)
                    }
                    Keys.onDownPressed: {
                        var nextIndex = optionItem.index + 1
                        if (nextIndex < repeater.count) {
                            var nextItem = repeater.itemAt(nextIndex)
                            if (nextItem) nextItem.focusArea.forceActiveFocus()
                        }
                    }
                    Keys.onUpPressed: {
                        var prevIndex = optionItem.index - 1
                        if (prevIndex >= 0) {
                            var prevItem = repeater.itemAt(prevIndex)
                            if (prevItem) prevItem.focusArea.forceActiveFocus()
                        }
                    }
                }
            }
        }
    }
}
