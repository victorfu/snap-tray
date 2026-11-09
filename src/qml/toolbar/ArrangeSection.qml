import QtQuick
import SnapTrayQml

/**
 * ArrangeSection: drawing order of the selected annotation (#268).
 *
 * Layout: [Send to Back] [Send Backward] [Bring Forward] [Bring to Front]
 * Buttons that would change nothing (already at that end) are dimmed.
 */
Item {
    id: root
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property var optionsModel: root.hasViewModel ? root.viewModel.zOrderOptions : []

    readonly property int sectionPadding: 3
    readonly property int buttonSize: 22
    readonly property int buttonSpacing: 2
    readonly property int iconSize: 14
    readonly property real disabledOpacity: 0.35

    implicitWidth: sectionPadding + buttonRow.width + 1
    implicitHeight: buttonSize
    width: implicitWidth
    height: implicitHeight

    readonly property color btnHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)

    Row {
        id: buttonRow
        spacing: root.buttonSpacing
        anchors.left: parent.left
        anchors.leftMargin: root.sectionPadding
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: root.optionsModel

            Rectangle {
                id: arrangeButton
                objectName: "arrangeButton_" + modelData.iconKey
                readonly property bool available: root.hasViewModel
                    && (modelData.raises ? root.viewModel.canRaiseSelection
                                         : root.viewModel.canLowerSelection)
                readonly property string fullName: modelData.shortcut.length > 0
                    ? modelData.label + " (" + modelData.shortcut + ")"
                    : modelData.label
                function trigger() {
                    if (arrangeButton.available)
                        root.viewModel.handleZOrderRequested(modelData.value)
                }
                width: root.buttonSize
                height: root.buttonSize
                radius: 4
                color: arrangeButton.available && arrangeMouse.containsMouse
                    ? root.btnHoverBg : "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: arrangeButton.fullName
                Accessible.onPressAction: arrangeButton.trigger()

                SvgIcon {
                    anchors.centerIn: parent
                    source: "qrc:/icons/icons/" + modelData.iconKey + ".svg"
                    iconSize: root.iconSize
                    color: ComponentTokens.toolbarIcon
                    opacity: arrangeButton.available ? 1.0 : root.disabledOpacity
                }

                MouseArea {
                    id: arrangeMouse
                    anchors.fill: parent
                    cursorShape: CursorTokens.toolbarControl
                    hoverEnabled: true
                    onClicked: arrangeButton.trigger()
                }
            }
        }
    }
}
