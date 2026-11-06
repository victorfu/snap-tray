import QtQuick
import SnapTrayQml

/**
 * SizeSection: Step badge size selector (S/M/L).
 */
Item {
    id: root
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property var sizeOptionsModel: root.hasViewModel ? root.viewModel.stepBadgeSizeOptions : []
    readonly property int stepBadgeSizeValue: root.hasViewModel ? root.viewModel.stepBadgeSize : 1

    readonly property int sectionPadding: 3

    implicitWidth: sectionPadding + sizeRow.width + 1
    implicitHeight: 22
    width: implicitWidth
    height: implicitHeight

    readonly property color btnHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)
    readonly property color btnActiveBg: DesignSystem.accentDefault

    Row {
        id: sizeRow
        spacing: 2
        anchors.left: parent.left
        anchors.leftMargin: root.sectionPadding
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: root.sizeOptionsModel

            Rectangle {
                width: 22
                height: 22
                radius: 4
                // Screen readers (#248): the full size name, not the S/M/L dot.
                Accessible.role: Accessible.RadioButton
                Accessible.name: [qsTr("Small"), qsTr("Medium"), qsTr("Large")][modelData.value] || modelData.label
                Accessible.checkable: true
                Accessible.checked: root.stepBadgeSizeValue === modelData.value
                Accessible.onPressAction: {
                    if (root.hasViewModel)
                        root.viewModel.handleStepBadgeSizeSelected(modelData.value)
                }
                color: root.stepBadgeSizeValue === modelData.value ? root.btnActiveBg
                     : sizeMouse.containsMouse ? root.btnHoverBg
                     : "transparent"

                Text {
                    visible: false
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: {
                        if (modelData.value === 0)
                            return 6
                        if (modelData.value === 1)
                            return 10
                        return 14
                    }
                    height: width
                    radius: width / 2
                    color: root.stepBadgeSizeValue === modelData.value
                        ? ComponentTokens.toolbarIconActive
                        : ComponentTokens.toolbarIcon
                }

                MouseArea {
                    id: sizeMouse
                    anchors.fill: parent
                    cursorShape: CursorTokens.toolbarControl
                    hoverEnabled: true
                    onClicked: {
                        if (root.hasViewModel)
                            root.viewModel.handleStepBadgeSizeSelected(modelData.value)
                    }
                }
            }
        }
    }
}
