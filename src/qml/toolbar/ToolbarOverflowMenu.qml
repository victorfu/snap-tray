import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml
import "ToolbarButtonState.js" as ButtonState

Item {
    id: root
    required property var viewModel
    property var buttons: []
    property int maximumWidth: 260
    property int maximumHeight: 400
    property int selectedIndex: -1
    readonly property int rowHeight: 34
    readonly property int separatorHeight: 8
    readonly property int menuContentHeight: buttons.reduce(function(total, button, index) {
        return total + rowHeight + (index > 0 && button.separatorBefore ? separatorHeight : 0)
    }, 0)
    signal actionTriggered(int buttonId)

    width: Math.max(1, Math.min(260, maximumWidth))
    height: Math.max(1, Math.min(maximumHeight, menuContentHeight + 12))

    function moveSelection(direction) {
        if (!buttons.length) return
        var next = selectedIndex
        if (next < 0) next = direction > 0 ? -1 : 0
        for (var i = 0; i < buttons.length; ++i) {
            next = (next + direction + buttons.length) % buttons.length
            if (!ButtonState.isDisabled(buttons[next], viewModel)) {
                selectedIndex = next
                list.positionViewAtIndex(next, ListView.Contain)
                return
            }
        }
    }
    function activateSelected() {
        if (selectedIndex >= 0 && selectedIndex < buttons.length
                && !ButtonState.isDisabled(buttons[selectedIndex], viewModel))
            actionTriggered(buttons[selectedIndex].id)
    }

    GlassSurface { anchors.fill: parent; glassRadius: 8 }
    ListView {
        id: list
        objectName: "toolbarOverflowList"
        anchors.fill: parent
        anchors.margins: Math.min(6, root.width / 4, root.height / 4)
        clip: true
        model: root.buttons
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Item {
            id: row
            required property var modelData
            required property int index
            readonly property bool separated: index > 0 && modelData.separatorBefore === true
            readonly property bool disabled: ButtonState.isDisabled(modelData, root.viewModel)
            readonly property bool active: root.viewModel && root.viewModel.activeTool === modelData.id
            objectName: "overflowButton_" + modelData.id
            width: list.width
            height: root.rowHeight + (separated ? root.separatorHeight : 0)
            Rectangle {
                visible: row.separated
                width: parent.width
                height: 1
                y: 3
                color: ComponentTokens.toolbarSeparator
            }
            Rectangle {
                anchors.fill: actionArea
                radius: 4
                color: row.active ? DesignSystem.accentDefault : ComponentTokens.thumbnailCardHover
                visible: row.active || root.selectedIndex === row.index || mouse.containsMouse
            }
            Item {
                id: actionArea
                y: row.separated ? root.separatorHeight : 0
                width: parent.width
                height: root.rowHeight
                opacity: row.disabled ? 0.4 : 1
                SvgIcon {
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    iconSize: 16
                    source: row.modelData.iconSource
                    color: row.active ? ComponentTokens.toolbarIconActive : ComponentTokens.toolbarIcon
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 30
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.tooltip
                    color: row.active ? ComponentTokens.toolbarIconActive : SemanticTokens.textPrimary
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    elide: Text.ElideRight
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !row.disabled
                    onClicked: root.actionTriggered(row.modelData.id)
                    onEntered: root.selectedIndex = row.index
                }
            }
        }
    }
}
