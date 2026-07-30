import QtQuick
import SnapTrayQml

/**
 * WidthSection: Stroke width preview, value readout, and up/down stepper.
 *
 * The preview dot scales with the current width. The stepper makes the value
 * visibly adjustable; the mouse wheel (handled by ToolOptionsStrip) still works
 * and is the fine-adjustment path.
 */
Item {
    id: root
    property var viewModel: null
    signal hoverEntered()
    signal hoverExited()

    readonly property bool hasViewModel: root.viewModel !== null && root.viewModel !== undefined
    readonly property int currentWidthValue: root.hasViewModel ? root.viewModel.currentWidth : 1
    readonly property int minWidthValue: root.hasViewModel ? root.viewModel.minWidth : 1
    readonly property int maxWidthValue: root.hasViewModel ? root.viewModel.maxWidth : 1

    readonly property int repeatDelayMs: 400
    readonly property int repeatIntervalMs: 60

    implicitWidth: 48
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight

    function stepBy(delta) {
        if (!root.hasViewModel)
            return
        var next = root.currentWidthValue + delta
        if (next < root.minWidthValue || next > root.maxWidthValue)
            return
        root.viewModel.handleWidthChanged(next)
    }

    component StepButton: Item {
        id: stepButton

        required property int delta
        required property bool stepEnabled

        signal activated()

        function activate() {
            if (!stepButton.stepEnabled)
                return
            root.stepBy(stepButton.delta)
            stepButton.activated()
        }

        width: 8
        height: 9
        opacity: stepButton.stepEnabled ? (stepMouse.containsMouse ? 1.0 : 0.55) : 0.25

        onVisibleChanged: {
            if (!visible)
                repeatTimer.stop()
        }

        ToolbarChevron {
            anchors.centerIn: parent
            rotation: stepButton.delta > 0 ? 180 : 0
        }

        MouseArea {
            id: stepMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: stepButton.stepEnabled
            onPressed: {
                stepButton.activate()
                repeatTimer.interval = root.repeatDelayMs
                repeatTimer.restart()
            }
            onReleased: repeatTimer.stop()
            onCanceled: repeatTimer.stop()
            onExited: repeatTimer.stop()
        }

        Timer {
            id: repeatTimer
            repeat: true
            onTriggered: {
                if (!stepButton.stepEnabled) {
                    repeatTimer.stop()
                    return
                }
                repeatTimer.interval = root.repeatIntervalMs
                stepButton.activate()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        z: -1
        onEntered: root.hoverEntered()
        onExited: root.hoverExited()
    }

    Row {
        objectName: "widthContentRow"
        anchors.centerIn: parent
        spacing: 2

        Rectangle {
            id: widthPreviewContainer
            objectName: "widthPreviewContainer"
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            radius: 5
            color: DesignSystem.accentDefault

            Rectangle {
                id: widthDot
                objectName: "widthPreviewDot"

                readonly property real minDot: 4
                readonly property real maxDot: 20
                readonly property real ratio: (root.currentWidthValue - root.minWidthValue) /
                                              Math.max(1, root.maxWidthValue - root.minWidthValue)

                width: Math.round(minDot + ratio * (maxDot - minDot))
                height: width
                x: (parent.width - width) / 2
                y: (parent.height - height) / 2
                radius: width / 2
                color: "white"
            }
        }

        Text {
            objectName: "widthValueLabel"
            anchors.verticalCenter: parent.verticalCenter
            text: root.currentWidthValue
            font.pixelSize: 12
            color: ComponentTokens.toolbarIcon
            horizontalAlignment: Text.AlignHCenter
            width: 14
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            StepButton {
                objectName: "widthStepUp"
                delta: 1
                stepEnabled: root.currentWidthValue < root.maxWidthValue
            }

            StepButton {
                objectName: "widthStepDown"
                delta: -1
                stepEnabled: root.currentWidthValue > root.minWidthValue
            }
        }
    }
}
