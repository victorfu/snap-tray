import QtQuick
import SnapTrayQml

/**
 * ShapeSection: Shape type (rectangle/ellipse) + fill mode (outline/filled) toggle.
 *
 * Layout: [Rectangle] [Ellipse] | [FillToggle]
 */
Item {
    id: root
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property var shapeOptionsModel: root.hasViewModel ? root.viewModel.shapeOptions : []
    readonly property var shapeFillOptionsModel: root.hasViewModel ? root.viewModel.shapeFillOptions : []
    readonly property int shapeTypeValue: root.hasViewModel ? root.viewModel.shapeType : 0
    readonly property int shapeFillModeValue: root.hasViewModel ? root.viewModel.shapeFillMode : 0

    readonly property int sectionPadding: 3

    implicitWidth: sectionPadding + shapeRow.width + 2 + fillBtn.width + 1
    implicitHeight: 22
    width: implicitWidth
    height: implicitHeight

    readonly property color btnHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)
    readonly property color btnActiveBg: DesignSystem.accentDefault

    // Shape type buttons
    Row {
        id: shapeRow
        spacing: 2
        anchors.left: parent.left
        anchors.leftMargin: root.sectionPadding
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: root.shapeOptionsModel

            Rectangle {
                width: 22
                height: 22
                radius: 4
                color: root.shapeTypeValue === modelData.value ? root.btnActiveBg
                     : shapeMouse.containsMouse ? root.btnHoverBg
                     : "transparent"

                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/" + modelData.iconKey + ".svg"
                    iconSize: 14
                    color: root.shapeTypeValue === modelData.value
                        ? ComponentTokens.toolbarIconActive
                        : ComponentTokens.toolbarIcon
                }

                MouseArea {
                    id: shapeMouse
                    anchors.fill: parent
                    cursorShape: CursorTokens.toolbarControl
                    hoverEnabled: true
                    onClicked: {
                        if (root.hasViewModel)
                            root.viewModel.handleShapeTypeSelected(modelData.value)
                    }
                }
            }
        }
    }

    // Fill mode toggle
    Rectangle {
        id: fillBtn
        anchors.left: shapeRow.right
        anchors.leftMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 22
        height: 22
        radius: 4
        color: root.shapeFillModeValue === 1 ? root.btnActiveBg
             : fillMouse.containsMouse ? root.btnHoverBg
             : "transparent"

        SvgIcon {
            anchors.centerIn: parent
            source: {
                var opts = root.shapeFillOptionsModel
                for (var i = 0; i < opts.length; i++) {
                    if (opts[i].value === root.shapeFillModeValue)
                        return "qrc:/icons/icons/" + opts[i].iconKey + ".svg"
                }
                return "qrc:/icons/icons/shape-outline.svg"
            }
            iconSize: 14
            color: root.shapeFillModeValue === 1
                ? ComponentTokens.toolbarIconActive
                : ComponentTokens.toolbarIcon
        }

        MouseArea {
            id: fillMouse
            anchors.fill: parent
            cursorShape: CursorTokens.toolbarControl
            hoverEnabled: true
            onClicked: {
                if (!root.hasViewModel)
                    return
                // Toggle between 0 (outline) and 1 (filled)
                var next = root.shapeFillModeValue === 0 ? 1 : 0
                root.viewModel.handleShapeFillModeSelected(next)
            }
        }
    }
}
