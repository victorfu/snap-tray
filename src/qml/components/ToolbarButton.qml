import QtQuick
import SnapTrayQml

/**
 * ToolbarButton: Reusable toolbar icon button with hover/active states.
 *
 * Visual structure:
 *   - Hover background rectangle (2px inset)
 *   - Centered SvgIcon with state-dependent color
 *   - MouseArea with hover tracking
 *
 * Usage:
 *   ToolbarButton {
 *       buttonId: 0
 *       iconSource: "qrc:/icons/icons/pencil.svg"
 *       isActive: viewModel.activeTool === 0
 *       onClicked: function(id) { viewModel.toolSelected(id) }
 *   }
 */
Item {
    id: root

    property int buttonId: -1
    property string iconSource: ""
    property bool isAction: false
    property bool isCancel: false
    property bool isRecord: false
    property bool isActive: false
    property bool isDisabled: false
    property string tooltipText: ""
    property bool separatorBefore: false

    // Icon color overrides (default to toolbar tokens)
    property color iconNormalColor: ComponentTokens.toolbarIcon
    property color iconActionColor: SemanticTokens.isDarkMode
        ? DesignSystem.accentLight : DesignSystem.accentDefault
    property color iconCancelColor: SemanticTokens.isDarkMode
        ? DesignSystem.red400 : DesignSystem.red500
    property color iconActiveColor: ComponentTokens.toolbarIconActive
    property color hoverBgColor: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(220 / 255, 220 / 255, 220 / 255, 1.0)
    property color activeBgColor: DesignSystem.accentDefault
    property int buttonRadius: 4
    property int iconSize: 16

    width: 28
    height: 24
    opacity: root.isDisabled ? 0.4 : 1.0

    signal clicked(int buttonId)
    signal hovered(int buttonId, real globalX, real globalY, real w, real h)
    signal unhovered()

    // Active background
    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: root.buttonRadius
        color: root.activeBgColor
        visible: root.isActive && !root.isDisabled
    }

    // Hover background (only when not active)
    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: root.buttonRadius
        color: root.hoverBgColor
        visible: mouseArea.containsMouse && !root.isActive && !root.isDisabled
    }

    // Icon
    SvgIcon {
        anchors.centerIn: parent
        source: root.iconSource
        iconSize: root.iconSize
        color: {
            if (root.isActive)
                return root.iconActiveColor;
            if (root.isCancel || root.isRecord)
                return root.iconCancelColor;
            if (root.isAction)
                return root.iconActionColor;
            return root.iconNormalColor;
        }
    }

    // Interaction
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: CursorTokens.toolbarControl

        onClicked: {
            if (!root.isDisabled)
                root.clicked(root.buttonId)
        }

        onContainsMouseChanged: {
            if (containsMouse) {
                var mapped = root.mapToItem(null, 0, 0)
                root.hovered(root.buttonId, mapped.x, mapped.y,
                             root.width, root.height)
            } else {
                root.unhovered()
            }
        }
    }
}
