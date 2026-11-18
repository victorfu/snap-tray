import QtQuick
import SnapTrayQml

/**
 * CutOutSection: remove or insert, band direction, the resulting size and
 * apply / cancel for the cut-out tool (#371).
 *
 * Layout: [Remove] [Insert] | [Horizontal] [Vertical] | -120 px > 800 x 480 px | [Cancel] [Apply]
 */
Item {
    id: root
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property bool hasDraft: root.hasViewModel && root.viewModel.cutOutHasDraft

    readonly property int sectionPadding: 3
    readonly property int buttonHeight: 22
    readonly property int buttonTextPadding: 12
    readonly property int groupGap: 8
    readonly property int labelPixelSize: 11
    readonly property real disabledOpacity: 0.4

    implicitWidth: contentRow.implicitWidth + sectionPadding
    implicitHeight: buttonHeight
    width: implicitWidth
    height: implicitHeight

    readonly property color btnHoverBg: SemanticTokens.isDarkMode
        ? Qt.rgba(80 / 255, 80 / 255, 80 / 255, 1.0)
        : Qt.rgba(232 / 255, 232 / 255, 232 / 255, 1.0)
    readonly property color btnActiveBg: DesignSystem.accentDefault

    // One exclusive choice of a group (operation or direction).
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
            model: root.hasViewModel ? root.viewModel.cutOutOperationOptions : []

            ChoiceButton {
                objectName: "cutOutOperationButton" + modelData.value
                label: modelData.label
                spokenName: modelData.name
                checked: root.hasViewModel && root.viewModel.cutOutOperation === modelData.value
                onChosen: if (root.hasViewModel) root.viewModel.handleCutOutOperationSelected(modelData.value)
            }
        }

        Item { width: root.groupGap; height: root.buttonHeight }

        Repeater {
            model: root.hasViewModel ? root.viewModel.cutOutAxisOptions : []

            ChoiceButton {
                objectName: "cutOutAxisButton" + modelData.value
                label: modelData.label
                spokenName: modelData.name
                checked: root.hasViewModel && root.viewModel.cutOutAxis === modelData.value
                onChosen: if (root.hasViewModel) root.viewModel.handleCutOutAxisSelected(modelData.value)
            }
        }

        // What changes and the size that results; empty until a strip is marked.
        Item {
            width: sizeLabel.implicitWidth + root.buttonTextPadding
            height: root.buttonHeight
            visible: root.hasDraft

            Text {
                id: sizeLabel
                objectName: "cutOutSizeLabel"
                anchors.centerIn: parent
                text: root.hasViewModel ? root.viewModel.cutOutSizeText : ""
                font.pixelSize: root.labelPixelSize
                color: ComponentTokens.toolbarIcon
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        // Why nothing can be marked right now (an odd zoom level).
        Item {
            width: hintLabel.implicitWidth + root.buttonTextPadding
            height: root.buttonHeight
            visible: hintLabel.text.length > 0

            Text {
                id: hintLabel
                objectName: "cutOutHintLabel"
                anchors.centerIn: parent
                text: root.hasViewModel ? root.viewModel.cutOutHintText : ""
                font.pixelSize: root.labelPixelSize
                color: ComponentTokens.toolbarIcon
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        Rectangle {
            id: cancelButton
            objectName: "cutOutCancelButton"
            width: cancelLabel.implicitWidth + root.buttonTextPadding
            height: root.buttonHeight
            radius: 4
            enabled: root.hasDraft
            opacity: enabled ? 1.0 : root.disabledOpacity
            color: cancelMouse.containsMouse && enabled ? root.btnHoverBg : "transparent"
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Cancel cut out")
            Accessible.onPressAction: cancel()

            function cancel() {
                if (root.hasViewModel && enabled)
                    root.viewModel.handleCutOutCancel()
            }

            Text {
                id: cancelLabel
                anchors.centerIn: parent
                text: qsTr("Cancel")
                font.pixelSize: root.labelPixelSize
                color: ComponentTokens.toolbarIcon
            }

            MouseArea {
                id: cancelMouse
                anchors.fill: parent
                cursorShape: CursorTokens.toolbarControl
                hoverEnabled: true
                onClicked: cancelButton.cancel()
            }
        }

        Rectangle {
            id: applyButton
            objectName: "cutOutApplyButton"
            width: applyLabel.implicitWidth + root.buttonTextPadding
            height: root.buttonHeight
            radius: 4
            enabled: root.hasDraft
            opacity: enabled ? 1.0 : root.disabledOpacity
            color: root.btnActiveBg
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Apply cut out")
            Accessible.onPressAction: apply()

            function apply() {
                if (root.hasViewModel && enabled)
                    root.viewModel.handleCutOutApply()
            }

            Text {
                id: applyLabel
                anchors.centerIn: parent
                text: qsTr("Apply")
                font.pixelSize: root.labelPixelSize
                color: ComponentTokens.toolbarIconActive
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: CursorTokens.toolbarControl
                hoverEnabled: true
                onClicked: applyButton.apply()
            }
        }
    }
}
