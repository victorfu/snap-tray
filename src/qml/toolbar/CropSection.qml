import QtQuick
import SnapTrayQml

/**
 * CropSection: aspect presets, the draft's pixel size and apply / cancel (#358).
 *
 * Layout: [Free] [Original] [1:1] [16:9] ... | 640 × 360 px | [Cancel] [Apply]
 */
Item {
    id: root
    property var viewModel: null
    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property var aspectOptionsModel: root.hasViewModel ? root.viewModel.cropAspectOptions : []
    readonly property int aspectValue: root.hasViewModel ? root.viewModel.cropAspect : 0
    readonly property bool hasDraft: root.hasViewModel && root.viewModel.cropHasDraft

    readonly property int sectionPadding: 3
    readonly property int buttonHeight: 22
    readonly property int buttonTextPadding: 12
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

    Row {
        id: contentRow
        spacing: 2
        anchors.left: parent.left
        anchors.leftMargin: root.sectionPadding
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: root.aspectOptionsModel

            Rectangle {
                objectName: "cropAspectButton" + modelData.value
                readonly property bool checked: root.aspectValue === modelData.value
                width: aspectLabel.implicitWidth + root.buttonTextPadding
                height: root.buttonHeight
                radius: 4
                color: checked ? root.btnActiveBg
                     : aspectMouse.containsMouse ? root.btnHoverBg
                     : "transparent"
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.name
                Accessible.checkable: true
                Accessible.checked: checked
                Accessible.onPressAction: select()

                function select() {
                    if (root.hasViewModel)
                        root.viewModel.handleCropAspectSelected(modelData.value)
                }

                Text {
                    id: aspectLabel
                    anchors.centerIn: parent
                    text: modelData.label
                    font.pixelSize: root.labelPixelSize
                    color: parent.checked ? ComponentTokens.toolbarIconActive : ComponentTokens.toolbarIcon
                }

                MouseArea {
                    id: aspectMouse
                    anchors.fill: parent
                    cursorShape: CursorTokens.toolbarControl
                    hoverEnabled: true
                    onClicked: parent.select()
                }
            }
        }

        // Size of the draft in source pixels; empty until something is selected.
        Item {
            width: sizeLabel.implicitWidth + root.buttonTextPadding
            height: root.buttonHeight
            visible: root.hasDraft

            Text {
                id: sizeLabel
                objectName: "cropSizeLabel"
                anchors.centerIn: parent
                text: root.hasViewModel ? root.viewModel.cropSizeText : ""
                font.pixelSize: root.labelPixelSize
                color: ComponentTokens.toolbarIcon
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }

        Rectangle {
            id: cancelButton
            objectName: "cropCancelButton"
            width: cancelLabel.implicitWidth + root.buttonTextPadding
            height: root.buttonHeight
            radius: 4
            enabled: root.hasDraft
            opacity: enabled ? 1.0 : root.disabledOpacity
            color: cancelMouse.containsMouse && enabled ? root.btnHoverBg : "transparent"
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Cancel crop")
            Accessible.onPressAction: cancel()

            function cancel() {
                if (root.hasViewModel && enabled)
                    root.viewModel.handleCropCancel()
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
            objectName: "cropApplyButton"
            width: applyLabel.implicitWidth + root.buttonTextPadding
            height: root.buttonHeight
            radius: 4
            enabled: root.hasDraft
            opacity: enabled ? 1.0 : root.disabledOpacity
            color: root.btnActiveBg
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Apply crop")
            Accessible.onPressAction: apply()

            function apply() {
                if (root.hasViewModel && enabled)
                    root.viewModel.handleCropApply()
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
