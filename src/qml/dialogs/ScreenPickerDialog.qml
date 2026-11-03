pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
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

    // The title and button bars, and room kept around the dialog: on a short
    // screen the setup scrolls instead of growing past it, so every option
    // and Start/Cancel stay reachable (PR #204 review).
    readonly property int dialogChromeHeight: 72 + 56
    readonly property int screenMargin: 80
    readonly property int minimumContentHeight: 160
    // The screen's usable height (a test sets a short one).
    property real availableScreenHeight: Screen.desktopAvailableHeight

    contentItem: Component {
        Item {
            readonly property real maximumHeight: Math.max(root.minimumContentHeight,
                root.availableScreenHeight - root.dialogChromeHeight - root.screenMargin)
            implicitHeight: Math.min(contentColumn.implicitHeight + 28, maximumHeight)

            Flickable {
            id: setupScroller
            objectName: "setupScroller"
            anchors.fill: parent
            anchors.margins: 14
            contentWidth: width
            contentHeight: contentColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: contentColumn
                width: setupScroller.width
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

                    // Quick or edit mode (#193), shared with Settings > Recording.
                    SettingsCombo {
                        objectName: "setupRecordingMode"
                        width: parent.width
                        label: qsTr("After recording")
                        visible: root.viewModel ? root.viewModel.recordingModeSupported : false
                        model: [
                            { text: qsTr("Edit in the preview"), value: true },
                            { text: qsTr("Save right away"), value: false }
                        ]
                        currentIndex: root.viewModel && root.viewModel.editMode ? 0 : 1
                        onActivated: function(index) { root.viewModel.editMode = index === 0 }
                    }

                    Text {
                        objectName: "setupRecordingModeNote"
                        width: parent.width
                        wrapMode: Text.Wrap
                        visible: root.viewModel ? root.viewModel.recordingModeSupported : false
                        text: root.viewModel && root.viewModel.editMode
                            ? qsTr("Edit mode: trim, crop and add effects in the preview, then save. Pointer, window and key tracks are kept for editing.")
                            : qsTr("Quick mode: the file is saved in its final format as soon as you stop, with no preview and no editing tracks.")
                        font.pixelSize: SemanticTokens.fontSizeCaption
                        font.family: SemanticTokens.fontFamily
                        color: SemanticTokens.textSecondary
                    }

                    SettingsCombo {
                        objectName: "setupQuickSaveAction"
                        width: parent.width
                        label: qsTr("After saving")
                        visible: root.viewModel ? !root.viewModel.editMode : false
                        model: [
                            { text: qsTr("Do nothing"), value: 0 },
                            { text: qsTr("Copy the file"), value: 1 },
                            { text: qsTr("Show in folder"), value: 2 }
                        ]
                        currentIndex: root.viewModel ? root.viewModel.quickSaveAction : 0
                        onActivated: function(index) { root.viewModel.quickSaveAction = index }
                    }

                    SettingsToggle {
                        objectName: "setupPreselectAreaToggle"
                        width: parent.width
                        label: qsTr("Choose an area first")
                        description: root.viewModel && root.viewModel.editMode
                            ? qsTr("The whole screen is still recorded; the preview opens cropped to the area.")
                            : qsTr("Only the area is recorded.")
                        checked: root.viewModel ? root.viewModel.preselectArea : false
                        onToggled: function(checked) { root.viewModel.preselectArea = checked }
                    }

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
                        objectName: "setupRecordKeysToggle"
                        width: parent.width
                        label: qsTr("Record shortcut keys")
                        description: root.viewModel && !root.viewModel.keyRecordingSupported
                            ? qsTr("Not available on this system yet")
                            : root.viewModel && !root.viewModel.editMode
                                ? qsTr("Edit mode only: quick mode keeps no editing tracks.")
                                : qsTr("For shortcut hints in the preview: keys held with Ctrl, Alt or Super, and keys like Enter or Esc. Kept with the temporary recording only.")
                        enabled: root.viewModel ? (root.viewModel.keyRecordingSupported && root.viewModel.editMode) : false
                        checked: root.viewModel ? root.viewModel.recordShortcutKeys : false
                        onToggled: function(checked) { root.viewModel.recordShortcutKeys = checked }
                    }

                    SettingsToggle {
                        objectName: "setupRecordTypedTextToggle"
                        width: parent.width
                        visible: root.viewModel ? (root.viewModel.recordShortcutKeys && root.viewModel.editMode) : false
                        label: qsTr("Also record typed text")
                        description: qsTr("Every key, including what you type. Do not use while entering passwords.")
                        checked: root.viewModel ? root.viewModel.recordTypedText : false
                        onToggled: function(checked) { root.viewModel.recordTypedText = checked }
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
