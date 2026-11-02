pragma ComponentBehavior: Bound

import QtQuick
import SnapTrayQml

DialogBase {
    id: root

    property var viewModel: null
    readonly property int cardWidth: 210
    readonly property int cardGap: 12
    readonly property int screenCount: viewModel ? viewModel.screens.length : 0
    readonly property var deviceOptions: buildDeviceOptions()

    function buildDeviceOptions() {
        var options = [{ text: qsTr("System Default"), value: "" }]
        var devices = viewModel ? viewModel.inputDevices : []
        for (var i = 0; i < devices.length; i++)
            options.push(devices[i])
        return options
    }

    function deviceIndex(deviceId) {
        for (var i = 0; i < deviceOptions.length; i++) {
            if (deviceOptions[i].value === deviceId)
                return i
        }
        return 0
    }

    // Load devices after the dialog is on screen so it never opens late.
    Component.onCompleted: Qt.callLater(function() {
        if (root.viewModel && !root.viewModel.devicesLoaded)
            root.viewModel.loadDevices()
    })

    title: qsTr("Record Screen")
    subtitle: screenCount > 1 ? qsTr("Choose a screen to start recording") : ""
    iconText: "\u23FA"
    dialogWidth: Math.min(704, Math.max(420, 28 + screenCount * cardWidth + Math.max(0, screenCount - 1) * cardGap))

    function dismiss() {
        if (viewModel) {
            viewModel.cancel()
            return
        }

        const window = root.Window.window
        if (window) {
            window.close()
        }
    }

    onCloseRequested: dismiss()

    contentItem: Component {
        Item {
            implicitHeight: contentColumn.implicitHeight + 28

            Column {
                id: contentColumn
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Flow {
                    width: parent.width
                    spacing: root.cardGap

                    Repeater {
                        model: root.viewModel ? root.viewModel.screens : []

                        ThumbnailCard {
                            required property var modelData
                            required property int index

                            thumbnailUrl: modelData.hasThumbnail
                                ? "image://dialog/" + modelData.thumbnailId + "?" + root.viewModel.thumbnailCacheBuster
                                : ""
                            titleText: modelData.name
                            subtitleText: modelData.resolution
                            tooltipText: modelData.tooltip
                            badgeText: ""
                            broken: !modelData.hasThumbnail
                            onClicked: if (root.viewModel) root.viewModel.chooseScreen(index)
                        }
                    }
                }

                // ── Recording setup ──
                Column {
                    objectName: "recordingSetup"
                    width: parent.width
                    spacing: 6
                    visible: root.viewModel !== null

                    SettingsToggle {
                        objectName: "setupMicrophoneToggle"
                        width: parent.width
                        label: qsTr("Microphone")
                        description: root.viewModel && !root.viewModel.outputHasAudio
                            ? qsTr("Not recorded with the current output format")
                            : ""
                        enabled: root.viewModel ? root.viewModel.outputHasAudio : false
                        checked: root.viewModel ? root.viewModel.microphoneEnabled : false
                        onToggled: function(checked) { root.viewModel.microphoneEnabled = checked }
                    }

                    SettingsCombo {
                        objectName: "setupDeviceCombo"
                        width: parent.width
                        label: qsTr("Input device")
                        model: root.deviceOptions
                        currentIndex: root.deviceIndex(root.viewModel ? root.viewModel.selectedDevice : "")
                        visible: root.viewModel ? (root.viewModel.microphoneEnabled && root.viewModel.devicesLoaded) : false
                        onActivated: function(index) {
                            root.viewModel.selectedDevice = root.deviceOptions[index].value
                        }
                    }

                    Row {
                        objectName: "setupMicrophoneTest"
                        spacing: 8
                        visible: root.viewModel ? root.viewModel.microphoneEnabled : false

                        DialogButton {
                            objectName: "setupMicrophoneTestButton"
                            height: 28
                            text: root.viewModel && root.viewModel.microphoneTestActive
                                ? qsTr("Stop Test") : qsTr("Test Microphone")
                            style: "secondary"
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                            Accessible.onPressAction: root.viewModel.toggleMicrophoneTest()
                            onClicked: root.viewModel.toggleMicrophoneTest()
                        }

                        // Input level meter, throttled in C++ (~15 updates/s).
                        Rectangle {
                            objectName: "setupMicrophoneLevel"
                            anchors.verticalCenter: parent.verticalCenter
                            width: 120
                            height: 6
                            radius: 3
                            color: SemanticTokens.backgroundOverlay
                            visible: root.viewModel ? root.viewModel.microphoneTestActive : false
                            Accessible.role: Accessible.ProgressBar
                            Accessible.name: qsTr("Microphone level")

                            Rectangle {
                                width: parent.width * (root.viewModel ? root.viewModel.microphoneLevel : 0)
                                height: parent.height
                                radius: 3
                                color: SemanticTokens.statusSuccess
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.viewModel ? root.viewModel.microphoneTestStatus : ""
                            font.pixelSize: SemanticTokens.fontSizeCaption
                            font.family: SemanticTokens.fontFamily
                            color: SemanticTokens.textSecondary
                        }
                    }

                    SettingsToggle {
                        objectName: "setupSystemAudioToggle"
                        width: parent.width
                        label: qsTr("System audio")
                        description: root.viewModel && root.viewModel.devicesLoaded && !root.viewModel.systemAudioSupported
                            ? qsTr("Not supported on this system")
                            : ""
                        enabled: root.viewModel
                            ? (root.viewModel.outputHasAudio && root.viewModel.systemAudioSupported)
                            : false
                        checked: root.viewModel ? root.viewModel.systemAudioEnabled : false
                        onToggled: function(checked) { root.viewModel.systemAudioEnabled = checked }
                    }

                    Text {
                        objectName: "setupOutputSummary"
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.viewModel ? root.viewModel.outputSummary : ""
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.family: SemanticTokens.fontFamily
                        color: SemanticTokens.textSecondary
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.viewModel ? root.viewModel.audioNote : ""
                        visible: text !== ""
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.family: SemanticTokens.fontFamily
                        color: SemanticTokens.statusWarning
                    }

                    SettingsToggle {
                        objectName: "setupShowBeforeRecordingToggle"
                        width: parent.width
                        label: qsTr("Show this setup before every recording")
                        checked: root.viewModel ? root.viewModel.showSetupBeforeRecording : false
                        onToggled: function(checked) { root.viewModel.showSetupBeforeRecording = checked }
                    }
                }
            }
        }
    }

    buttonBar: Component {
        Item {
            implicitHeight: 56

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8
                layoutDirection: Qt.RightToLeft

                DialogButton {
                    objectName: "setupStartButton"
                    text: qsTr("Start Recording")
                    style: "primary"
                    visible: root.screenCount === 1
                    onClicked: if (root.viewModel) root.viewModel.chooseScreen(0)
                }

                DialogButton {
                    text: qsTr("Cancel")
                    style: "secondary"
                    width: 100
                    onClicked: root.dismiss()
                }
            }
        }
    }
}
