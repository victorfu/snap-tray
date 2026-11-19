import QtQuick
import SnapTrayQml

/**
 * MeasureSection: manual line or image-based auto spans for the Measure
 * tool (#348).
 *
 * Layout: [Manual] [Auto] | hint (auto only)
 */
Item {
    id: root
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property bool autoMode: root.hasViewModel && root.viewModel.measureAutoMode
    signal tooltipRequested(string controlName, string text)
    signal tooltipDismissed(string controlName)

    readonly property int sectionPadding: 3
    readonly property int buttonHeight: 22
    readonly property int buttonTextPadding: 12
    readonly property int labelPixelSize: 11

    implicitWidth: contentRow.implicitWidth + sectionPadding
    implicitHeight: buttonHeight
    width: implicitWidth
    height: implicitHeight

    readonly property color btnHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)
    readonly property color btnActiveBg: DesignSystem.accentDefault

    component ChoiceButton: Rectangle {
        id: choice
        property string label: ""
        property string spokenName: ""
        property bool checked: false
        signal chosen()

        width: choiceLabel.implicitWidth + root.buttonTextPadding
        height: root.buttonHeight
        radius: 4
        color: checked ? root.btnActiveBg
             : choiceMouse.containsMouse ? root.btnHoverBg
             : "transparent"
        Accessible.role: Accessible.RadioButton
        Accessible.name: spokenName
        Accessible.checkable: true
        Accessible.checked: checked
        Accessible.onPressAction: choice.chosen()

        Text {
            id: choiceLabel
            anchors.centerIn: parent
            text: choice.label
            font.pixelSize: root.labelPixelSize
            color: choice.checked ? ComponentTokens.toolbarIconActive : ComponentTokens.toolbarIcon
        }

        MouseArea {
            id: choiceMouse
            anchors.fill: parent
            cursorShape: CursorTokens.toolbarControl
            hoverEnabled: true
            onClicked: choice.chosen()
        }
    }

    Row {
        id: contentRow
        spacing: 2
        anchors.left: parent.left
        anchors.leftMargin: root.sectionPadding
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: root.hasViewModel ? root.viewModel.measureModeOptions : []

            ChoiceButton {
                objectName: "measureModeButton" + modelData.value
                label: modelData.label
                spokenName: modelData.name
                checked: root.hasViewModel && root.viewModel.measureMode === modelData.value
                onChosen: if (root.hasViewModel) root.viewModel.handleMeasureModeSelected(modelData.value)
            }
        }

        // Auto values come from pixels, not from a layout: say so.
        Item {
            width: hintLabel.implicitWidth + root.buttonTextPadding
            height: root.buttonHeight
            visible: root.autoMode

            Text {
                id: hintLabel
                objectName: "measureHintLabel"
                readonly property string boxShortcutTip: root.hasViewModel
                    ? qsTr("Hold %1 when you start dragging to measure a box.").arg(root.viewModel.measureBoxModifierText)
                    : ""
                anchors.centerIn: parent
                text: qsTr("Hover to measure. Values are image estimates.")
                font.pixelSize: root.labelPixelSize
                color: ComponentTokens.toolbarIcon
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Accessible.description: boxShortcutTip

                HoverHandler {
                    onHoveredChanged: {
                        if (hovered)
                            root.tooltipRequested(hintLabel.objectName, hintLabel.boxShortcutTip)
                        else
                            root.tooltipDismissed(hintLabel.objectName)
                    }
                }
            }
        }
    }
}
