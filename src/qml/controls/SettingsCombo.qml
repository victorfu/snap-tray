import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * SettingsCombo: Dropdown row for selecting from a list of options.
 *
 * Accepts model as array of {text, value} objects. Custom styled with
 * token colors for the popup and items.
 */
SettingsRow {
    id: root

    property var model: []
    property int currentIndex: 0
    property var currentValue: model.length > 0 && currentIndex >= 0 && currentIndex < model.length
        ? model[currentIndex].value : undefined

    property int minimumPopupWidth: 0

    signal activated(int index)

    ComboBox {
        id: combo
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(120, implicitWidth)
        model: root.model
        currentIndex: root.currentIndex
        textRole: "text"

        onActivated: function(index) {
            root.activated(index)
        }

        background: Rectangle {
            color: ComponentTokens.inputBackground
            border.width: 1
            border.color: combo.activeFocus ? ComponentTokens.inputBorderFocus : ComponentTokens.inputBorder
            radius: ComponentTokens.inputRadius
            implicitHeight: ComponentTokens.inputHeight

            Behavior on border.color {
                ColorAnimation { duration: PrimitiveTokens.durationFast }
            }
        }

        contentItem: Text {
            leftPadding: 8
            rightPadding: combo.indicator.width + 8
            text: combo.displayText
            color: SemanticTokens.textPrimary
            font.pixelSize: PrimitiveTokens.fontSizeBody
            font.family: PrimitiveTokens.fontFamily
            font.letterSpacing: PrimitiveTokens.letterSpacingTight
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        indicator: Text {
            x: combo.width - width - 8
            anchors.verticalCenter: parent.verticalCenter
            text: "\u25BE"
            color: SemanticTokens.textSecondary
            font.pixelSize: PrimitiveTokens.fontSizeBody
            font.family: PrimitiveTokens.fontFamily
        }

        popup: Popup {
            y: combo.height + 2
            width: Math.max(combo.width, root.minimumPopupWidth)
            padding: 1

            background: Rectangle {
                color: ComponentTokens.panelBackground
                border.width: 1
                border.color: ComponentTokens.panelBorder
                radius: ComponentTokens.inputRadius
            }

            contentItem: ListView {
                clip: true
                implicitHeight: Math.min(contentHeight, ComponentTokens.comboPopupMaxHeight)
                model: combo.popup.visible ? combo.delegateModel : null
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
        }

        delegate: ItemDelegate {
            // Width accounts for popup padding on both sides
            width: combo.width - 2 * combo.popup.padding
            height: ComponentTokens.inputHeight
            highlighted: combo.highlightedIndex === index

            contentItem: Text {
                text: modelData.text
                color: SemanticTokens.textPrimary
                font.pixelSize: PrimitiveTokens.fontSizeBody
                font.family: PrimitiveTokens.fontFamily
                font.letterSpacing: PrimitiveTokens.letterSpacingTight
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignLeft
                leftPadding: PrimitiveTokens.spacing8
                LayoutMirroring.enabled: false
            }

            background: Rectangle {
                color: hovered ? ComponentTokens.settingsSidebarHoverItem : "transparent"

                Behavior on color {
                    ColorAnimation { duration: PrimitiveTokens.durationFast }
                }
            }
        }
    }
}
