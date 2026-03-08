import QtQuick
import SnapTrayQml

/**
 * ArrowStyleSection: Arrow end-style selector with dropdown.
 *
 * Shows current arrow style icon with dropdown arrow.
 * Clicking cycles through available arrow styles.
 */
Item {
    id: root
    objectName: "arrowStyleSection"
    required property var viewModel
    property bool expandUpward: false
    signal menuOpened()
    signal menuClosed()

    readonly property int buttonWidth: 52
    readonly property int buttonHeight: 22
    readonly property int buttonTop: 3
    readonly property int dropdownGap: 4
    readonly property int dropdownPadding: 4
    readonly property int dropdownSpacing: 2
    readonly property int dropdownRadius: 6
    readonly property int dropdownOptionHeight: 28
    readonly property int dropdownWidth: buttonWidth + 10
    readonly property var options: root.viewModel ? root.viewModel.arrowStyleOptions : []
    readonly property int dropdownContentHeight: root.dropdownPadding * 2 + optionsColumn.height
    readonly property int openExtraHeight: root.dropdownGap + root.dropdownContentHeight
    readonly property int buttonYOffset: root.expandUpward && root.dropdownOpen ? root.openExtraHeight : 0
    readonly property int popupOverflowLeft: root.dropdownOpen ? Math.max(0, -dropdownRect.x) : 0
    readonly property int anchorOffsetY: buttonRect.y
    property bool dropdownOpen: false
    property int hoveredOption: -1

    implicitWidth: buttonWidth
    implicitHeight: dropdownOpen
                    ? buttonTop + buttonHeight + root.openExtraHeight
                    : buttonTop + buttonHeight
    width: implicitWidth
    height: implicitHeight

    readonly property color btnInactiveBg: ComponentTokens.toolbarControlBackground
    readonly property color btnHoverBg: ComponentTokens.toolbarControlBackgroundHover
    readonly property color borderColor: SemanticTokens.borderDefault
    readonly property color popupHoverBg: ComponentTokens.toolbarPopupHover
    readonly property color popupSelectedBg: ComponentTokens.toolbarPopupSelected

    function rectContains(itemRect, localX, localY) {
        return localX >= itemRect.x
            && localX < itemRect.x + itemRect.width
            && localY >= itemRect.y
            && localY < itemRect.y + itemRect.height
    }

    function closeMenu() {
        dropdownOpen = false
        hoveredOption = -1
    }

    function openMenu() {
        dropdownOpen = true
        hoveredOption = -1
    }

    onDropdownOpenChanged: {
        if (dropdownOpen)
            root.menuOpened()
        else
            root.menuClosed()
    }

    function currentIconKey() {
        for (var i = 0; i < options.length; i++) {
            if (options[i].value === root.viewModel.arrowStyle)
                return options[i].iconKey
        }
        return "arrow-end"
    }

    function containsLocalPoint(localX, localY) {
        if (rectContains(buttonRect, localX, localY))
            return true

        if (dropdownOpen && rectContains(dropdownRect, localX, localY))
            return true

        return false
    }

    Rectangle {
        id: buttonRect
        y: root.buttonTop + root.buttonYOffset
        width: root.buttonWidth
        height: root.buttonHeight
        radius: 4
        color: root.hoveredOption === -2 ? root.btnHoverBg : root.btnInactiveBg
        border.width: 1
        border.color: root.dropdownOpen ? SemanticTokens.borderActive : root.borderColor

        SvgIcon {
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 16
            height: 14
            source: "qrc:/icons/icons/" + root.currentIconKey() + ".svg"
            iconSize: 14
            color: ComponentTokens.toolbarIcon
        }

        ToolbarChevron {
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            strokeColor: ComponentTokens.toolbarIcon
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.ArrowCursor

            onContainsMouseChanged: {
                root.hoveredOption = containsMouse ? -2 : -1
            }

            onClicked: {
                root.dropdownOpen = !root.dropdownOpen
                root.hoveredOption = root.dropdownOpen ? -1 : (containsMouse ? -2 : -1)
            }
        }
    }

    Rectangle {
        id: dropdownRect
        x: root.buttonWidth - root.dropdownWidth
        y: root.expandUpward ? root.buttonTop : buttonRect.y + buttonRect.height + root.dropdownGap
        width: root.dropdownWidth
        height: root.dropdownContentHeight
        radius: root.dropdownRadius
        visible: root.dropdownOpen
        z: 2

        GlassSurface {
            anchors.fill: parent
            glassBg: ComponentTokens.toolbarPopupBackground
            glassBgTop: ComponentTokens.toolbarPopupBackgroundTop
            glassHighlight: ComponentTokens.toolbarPopupHighlight
            glassBorder: ComponentTokens.toolbarPopupBorder
            glassRadius: root.dropdownRadius
        }

        Item {
            id: optionsClip
            anchors.fill: parent
            anchors.margins: root.dropdownPadding
            clip: true

            Column {
                id: optionsColumn
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: root.dropdownSpacing

                Repeater {
                    model: root.options

                    Rectangle {
                        width: optionsColumn.width
                        height: root.dropdownOptionHeight
                        radius: 5
                        color: {
                            if (root.viewModel.arrowStyle === modelData.value)
                                return root.popupSelectedBg
                            if (optionMouse.containsMouse)
                                return root.popupHoverBg
                            return "transparent"
                        }
                        border.width: root.viewModel.arrowStyle === modelData.value ? 1 : 0
                        border.color: ComponentTokens.toolbarPopupSelectedBorder

                        SvgIcon {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 16
                            height: 14
                            source: "qrc:/icons/icons/" + modelData.iconKey + ".svg"
                            iconSize: 14
                            color: ComponentTokens.toolbarIcon
                        }

                        MouseArea {
                            id: optionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.ArrowCursor

                            onClicked: {
                                root.viewModel.handleArrowStyleSelected(modelData.value)
                                root.closeMenu()
                            }
                        }
                    }
                }
            }
        }
    }
}
