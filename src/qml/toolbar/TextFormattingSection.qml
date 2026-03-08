import QtQuick
import SnapTrayQml

/**
 * TextFormattingSection: Bold/Italic/Underline toggles + font size/family dropdowns.
 *
 * Layout: [B] [I] [U] | [Font Size ▼] | [Font Family ▼]
 */
Item {
    id: root
    required property var viewModel

    readonly property int sectionPadding: 6
    readonly property int buttonSize: 20
    readonly property color btnInactiveBg: SemanticTokens.isDarkMode
        ? Qt.rgba(50 / 255, 50 / 255, 50 / 255, 1.0)
        : Qt.rgba(245 / 255, 245 / 255, 245 / 255, 1.0)

    implicitWidth: sectionPadding + bRow.width + 8 + fontSizeBtn.width + 8 + fontFamilyBtn.width + sectionPadding
    implicitHeight: 22
    width: implicitWidth
    height: implicitHeight

    readonly property color btnHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)
    readonly property color btnActiveBg: DesignSystem.accentDefault
    readonly property color btnText: ComponentTokens.toolbarIcon
    readonly property color btnActiveText: ComponentTokens.toolbarIconActive

    // B/I/U toggle row
    Row {
        id: bRow
        spacing: 2
        anchors.left: parent.left
        anchors.leftMargin: root.sectionPadding
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: [
                { label: "B", bold: true, active: root.viewModel.isBold, action: function() { root.viewModel.handleBoldToggled() } },
                { label: "I", italic: true, active: root.viewModel.isItalic, action: function() { root.viewModel.handleItalicToggled() } },
                { label: "U", underline: true, active: root.viewModel.isUnderline, action: function() { root.viewModel.handleUnderlineToggled() } }
            ]

            Rectangle {
                width: root.buttonSize
                height: root.buttonSize
                radius: 4
                color: modelData.active ? root.btnActiveBg
                     : biuMouse.containsMouse ? root.btnHoverBg
                     : root.btnInactiveBg

                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    font.pixelSize: 11
                    font.bold: modelData.bold || false
                    font.italic: modelData.italic || false
                    font.underline: modelData.underline || false
                    color: modelData.active ? root.btnActiveText : root.btnText
                }

                MouseArea {
                    id: biuMouse
                    anchors.fill: parent
                    cursorShape: Qt.ArrowCursor
                    hoverEnabled: true
                    onClicked: modelData.action()
                }
            }
        }
    }

    // Font size dropdown
    Rectangle {
        id: fontSizeBtn
        anchors.left: bRow.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 40
        height: root.buttonSize
        radius: 4
        color: fontSizeMouse.containsMouse ? root.btnHoverBg : root.btnInactiveBg
        border.width: 1
        border.color: SemanticTokens.borderDefault

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: root.viewModel.fontSize
            font.pixelSize: 10
            color: root.btnText
        }

        ToolbarChevron {
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            strokeColor: root.btnText
        }

        MouseArea {
            id: fontSizeMouse
            anchors.fill: parent
            cursorShape: Qt.ArrowCursor
            hoverEnabled: true
            onClicked: {
                var mapped = fontSizeBtn.mapToGlobal(0, fontSizeBtn.height)
                root.viewModel.handleFontSizeDropdown(mapped.x, mapped.y)
            }
        }
    }

    // Font family dropdown
    Rectangle {
        id: fontFamilyBtn
        anchors.left: fontSizeBtn.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 90
        height: root.buttonSize
        radius: 4
        color: fontFamilyMouse.containsMouse ? root.btnHoverBg : root.btnInactiveBg
        border.width: 1
        border.color: SemanticTokens.borderDefault

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.right: dropArrow2.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.viewModel.fontFamily || qsTr("Default")
            font.pixelSize: 10
            color: root.btnText
            elide: Text.ElideRight
        }

        ToolbarChevron {
            id: dropArrow2
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            strokeColor: root.btnText
        }

        MouseArea {
            id: fontFamilyMouse
            anchors.fill: parent
            cursorShape: Qt.ArrowCursor
            hoverEnabled: true
            onClicked: {
                var mapped = fontFamilyBtn.mapToGlobal(0, fontFamilyBtn.height)
                root.viewModel.handleFontFamilyDropdown(mapped.x, mapped.y)
            }
        }
    }
}
