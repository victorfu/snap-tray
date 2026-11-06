import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

/**
 * RecordingSettings: Frame rate, default output format, quality, audio, preview, countdown.
 */
Flickable {
    id: root
    clip: true
    contentHeight: content.height
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    readonly property var frameRates: [10, 15, 24, 30]
    readonly property var projectCleanupOptions: [
        { text: qsTr("Never"), value: 0 },
        { text: qsTr("7 days"), value: 7 },
        { text: qsTr("30 days"), value: 30 },
        { text: qsTr("90 days"), value: 90 }
    ]
    readonly property bool audioCaptureSupported: !settingsBackend.recordingDirectMp4Only
        && (settingsBackend.recordingShowPreview || settingsBackend.recordingOutputFormat === 0)
    readonly property bool audioSourceUsesInputDevice: settingsBackend.recordingAudioSource !== 1
    readonly property var audioDevices: settingsBackend.recordingAudioDevices
    readonly property bool audioDevicesLoading: settingsBackend.recordingAudioDevicesLoading
    readonly property bool audioDevicesLoaded: settingsBackend.recordingAudioDevicesLoaded
    readonly property bool showAudioDeviceLoading: root.audioCaptureSupported
        && root.audioSourceUsesInputDevice
        && !root.audioDevicesLoaded
    readonly property var audioDeviceOptions: buildAudioDeviceOptions()

    Component.onCompleted: {
        Qt.callLater(function() {
            if (!settingsBackend.recordingDirectMp4Only)
                settingsBackend.loadRecordingAudioDevices()
        })
    }

    function frameRateToIndex(fps) {
        for (var i = 0; i < frameRates.length; i++) {
            if (frameRates[i] === fps) return i
        }
        return 3 // default to 30
    }

    function buildAudioDeviceOptions() {
        var options = [{ text: qsTr("System Default"), value: "" }]
        var devices = root.audioDevices
        for (var i = 0; i < devices.length; i++) {
            options.push(devices[i])
        }
        return options
    }

    function audioDeviceToIndex(deviceId) {
        for (var i = 0; i < audioDeviceOptions.length; i++) {
            if (audioDeviceOptions[i].value === deviceId) return i
        }
        return 0
    }

    function formatInfoText() {
        if (settingsBackend.recordingDirectMp4Only)
            return qsTr("Recording saves silent MP4 files directly, without a preview.")
        if (settingsBackend.recordingShowPreview) {
            if (settingsBackend.recordingOutputFormat === 0) {
                return qsTr("Preview always records as MP4 for playback compatibility. This setting chooses the default export format shown in the preview window. MP4 is the only export format that keeps audio.")
            }
            if (settingsBackend.recordingOutputFormat === 2) {
                return qsTr("Preview always records as MP4 for playback compatibility. This setting chooses the default export format shown in the preview window. WebP exports do not include audio.")
            }
            return qsTr("Preview always records as MP4 for playback compatibility. This setting chooses the default export format shown in the preview window. GIF exports do not include audio.")
        }

        if (settingsBackend.recordingOutputFormat === 2) {
            return qsTr("WebP exports create smaller files than GIF with better quality for short clips. Audio is not supported for direct WebP recording.")
        }
        if (settingsBackend.recordingOutputFormat === 1) {
            return qsTr("GIF exports are best for short clips and quick sharing. Audio is not supported for direct GIF recording.")
        }
        return qsTr("MP4 records directly with H.264 video and optional audio.")
    }

    Column {
        id: content
        width: root.width
        padding: ComponentTokens.settingsContentPadding
        spacing: ComponentTokens.settingsColumnSpacing

        SettingsPageHeader {
            title: qsTr("Recording")
            description: qsTr("How screen recordings start, what they capture, and what happens when you stop.")
        }

        SettingsCombo {
            resetKey: "recordingFrameRate"
            label: qsTr("Frame rate")
            model: [
                { text: qsTr("10 FPS"), value: 10 },
                { text: qsTr("15 FPS"), value: 15 },
                { text: qsTr("24 FPS"), value: 24 },
                { text: qsTr("30 FPS"), value: 30 }
            ]
            currentIndex: root.frameRateToIndex(settingsBackend.recordingFrameRate)
            onActivated: function(index) {
                settingsBackend.recordingFrameRate = root.frameRates[index]
            }
        }

        SettingsCombo {
            resetKey: "recordingOutputFormat"
            label: qsTr("Default output format")
            enabled: !settingsBackend.recordingDirectMp4Only
            model: [
                { text: qsTr("MP4 (H.264)"), value: 0 },
                { text: qsTr("GIF"), value: 1 },
                { text: qsTr("WebP"), value: 2 }
            ]
            currentIndex: settingsBackend.recordingOutputFormat
            onActivated: function(index) { settingsBackend.recordingOutputFormat = index }
        }

        SettingsSlider {
            resetKey: "recordingQuality"
            label: qsTr("Recording quality")
            value: settingsBackend.recordingQuality
            from: 0
            to: 100
            visible: settingsBackend.recordingShowPreview || settingsBackend.recordingOutputFormat === 0
            onMoved: function(value) { settingsBackend.recordingQuality = value }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: settingsBackend.recordingShowPreview || settingsBackend.recordingOutputFormat === 0
            text: qsTr("Quality of the saved video. With preview on, recordings are captured at high quality and converted to this quality when you save.")
            color: SemanticTokens.textTertiary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
        }

        Rectangle {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: formatInfoTextItem.implicitHeight + 20
            radius: SemanticTokens.radiusSmall
            color: ComponentTokens.infoPanelBg
            border.width: 1
            border.color: ComponentTokens.infoPanelBorder
            visible: settingsBackend.recordingDirectMp4Only || settingsBackend.recordingShowPreview || settingsBackend.recordingOutputFormat > 0

            Text {
                id: formatInfoTextItem
                anchors.fill: parent
                anchors.margins: 10
                text: root.formatInfoText()
                color: ComponentTokens.infoPanelText
                font.pixelSize: SemanticTokens.fontSizeBody
                font.family: SemanticTokens.fontFamily
                font.letterSpacing: SemanticTokens.letterSpacingDefault
                wrapMode: Text.WordWrap
            }
        }

        SettingsSection { title: qsTr("Audio"); visible: !settingsBackend.recordingDirectMp4Only }

        SettingsToggle {
            resetKey: "recordingAudioEnabled"
            label: qsTr("Record audio")
            visible: !settingsBackend.recordingDirectMp4Only
            description: root.audioCaptureSupported
                ? (settingsBackend.recordingShowPreview
                    ? qsTr("Audio is captured in the temporary MP4 recording. Only MP4 export keeps audio.")
                    : "")
                : qsTr("Audio is only available for direct MP4 recording, or when Show preview is enabled.")
            checked: settingsBackend.recordingAudioEnabled
            enabled: root.audioCaptureSupported
            opacity: enabled ? 1.0 : 0.6
            onToggled: function(checked) { settingsBackend.recordingAudioEnabled = checked }
        }

        SettingsCombo {
            resetKey: "recordingAudioSource"
            label: qsTr("Source")
            visible: !settingsBackend.recordingDirectMp4Only
            model: [
                { text: qsTr("Microphone"), value: 0 },
                { text: qsTr("System Audio"), value: 1 },
                { text: qsTr("Both (Mixed)"), value: 2 }
            ]
            currentIndex: settingsBackend.recordingAudioSource
            enabled: root.audioCaptureSupported && settingsBackend.recordingAudioEnabled
            opacity: enabled ? 1.0 : 0.6
            onActivated: function(index) { settingsBackend.recordingAudioSource = index }
        }

        Rectangle {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            visible: root.showAudioDeviceLoading
            radius: SemanticTokens.radiusSmall
            color: ComponentTokens.infoPanelBg
            border.width: 1
            border.color: ComponentTokens.infoPanelBorder
            height: statusColumn.implicitHeight + 24

            Column {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: SemanticTokens.spacing8

                Row {
                    spacing: SemanticTokens.spacing8

                    BusySpinner {
                        running: root.showAudioDeviceLoading
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: qsTr("Loading audio devices...")
                        color: ComponentTokens.infoPanelText
                        font.pixelSize: SemanticTokens.fontSizeBody
                        font.weight: Font.Medium
                        font.family: SemanticTokens.fontFamily
                        font.letterSpacing: SemanticTokens.letterSpacingDefault
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Text {
                    width: parent.width
                    text: qsTr("The recording page is ready. Input devices are loading in the background.")
                    color: ComponentTokens.infoPanelText
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: SemanticTokens.fontFamily
                    font.letterSpacing: SemanticTokens.letterSpacingDefault
                    wrapMode: Text.WordWrap
                }
            }
        }

        SettingsCombo {

            objectName: "settingsInputDevice"
            label: qsTr("Input device")
            model: root.audioDeviceOptions
            currentIndex: root.audioDeviceToIndex(settingsBackend.recordingAudioDevice)
            visible: root.audioCaptureSupported && root.audioSourceUsesInputDevice && root.audioDevicesLoaded
            enabled: settingsBackend.recordingAudioEnabled
            opacity: enabled ? 1.0 : 0.6
            onActivated: function(index) {
                settingsBackend.recordingAudioDevice = root.audioDeviceOptions[index].value
            }
        }

        Spacer {}

        SettingsToggle {
            resetKey: "recordingShowSetup"
            label: qsTr("Show setup before recording")
            description: qsTr("Confirm the screen, microphone, and system audio before each recording. When off, recording starts right away with these settings.")
            checked: settingsBackend.recordingShowSetup
            onToggled: function(checked) { settingsBackend.recordingShowSetup = checked }
        }

        SettingsCombo {
            objectName: "settingsRecordingKeys"
            label: qsTr("Record keys")
            enabled: settingsBackend.recordingKeysSupported
            model: [
                { text: qsTr("Off"), value: 0 },
                { text: qsTr("Shortcuts only"), value: 1 },
                { text: qsTr("All keys, including typed text"), value: 2 }
            ]
            currentIndex: settingsBackend.recordingKeyMode
            onActivated: function(index) { settingsBackend.recordingKeyMode = index }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: settingsBackend.recordingKeysSupported
                ? qsTr("For shortcut hints in the preview. Keys stay with the temporary recording and are deleted with it; they are never in the saved file.")
                : qsTr("Not available on this system yet")
            color: SemanticTokens.textTertiary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
        }

        // Quick or edit mode (#193), also chosen in the recording setup.
        SettingsCombo {
            resetKey: "recordingShowPreview"
            objectName: "settingsRecordingMode"
            label: qsTr("After recording")
            visible: !settingsBackend.recordingDirectMp4Only
            model: [
                { text: qsTr("Edit in the preview"), value: true },
                { text: qsTr("Save right away"), value: false }
            ]
            currentIndex: settingsBackend.recordingShowPreview ? 0 : 1
            onActivated: function(index) { settingsBackend.recordingShowPreview = index === 0 }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: !settingsBackend.recordingDirectMp4Only
            text: settingsBackend.recordingShowPreview
                ? qsTr("Edit mode: trim, crop and add effects in the preview, then save. Preview recordings are always captured as MP4 first.")
                : qsTr("Quick mode: the file is saved in its final format as soon as you stop, with no preview and no editing tracks.")
            color: SemanticTokens.textTertiary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
        }

        SettingsCombo {
            objectName: "settingsQuickSaveAction"
            label: qsTr("After saving")
            visible: !settingsBackend.recordingShowPreview
            model: [
                { text: qsTr("Do nothing"), value: 0 },
                { text: qsTr("Copy the file"), value: 1 },
                { text: qsTr("Show in folder"), value: 2 }
            ]
            currentIndex: settingsBackend.recordingQuickSaveAction
            onActivated: function(index) { settingsBackend.recordingQuickSaveAction = index }
        }

        // An area chosen before recording (#194); off by default.
        SettingsToggle {
            resetKey: "recordingPreselectArea"
            objectName: "settingsPreselectArea"
            label: qsTr("Choose an area before recording")
            description: qsTr("Drag over the screen to pick what to record. Edit mode still records the whole screen and opens the preview cropped to the area, so you can change it; quick mode records only the area.")
            checked: settingsBackend.recordingPreselectArea
            onToggled: function(checked) { settingsBackend.recordingPreselectArea = checked }
        }

        // Editable projects (#191): off by default, the recording is private.
        SettingsToggle {
            resetKey: "recordingKeepProjects"
            objectName: "settingsKeepProjects"
            label: qsTr("Keep editable projects")
            visible: !settingsBackend.recordingDirectMp4Only
            // Projects are kept from the preview only (PR #203 review).
            enabled: settingsBackend.recordingShowPreview
            description: settingsBackend.recordingShowPreview
                ? qsTr("After saving, keep the recording and your edits so you can change and export it again from Recording Library. Projects keep the full screen recording, its pointer, keys and audio tracks, including anything you blurred or cropped out, and use disk space.")
                : qsTr("Projects are kept when you save from the preview. Turn on Show preview to use them.")
            checked: settingsBackend.recordingKeepProjects
            onToggled: function(checked) { settingsBackend.recordingKeepProjects = checked }
        }

        SettingsCombo {
            resetKey: "recordingProjectCleanupDays"
            objectName: "settingsProjectCleanup"
            label: qsTr("Delete projects not changed for")
            visible: !settingsBackend.recordingDirectMp4Only
            model: root.projectCleanupOptions
            currentIndex: Math.max(0, root.projectCleanupOptions.findIndex(function(o) {
                return o.value === settingsBackend.recordingProjectCleanupDays }))
            onActivated: function(index) {
                settingsBackend.recordingProjectCleanupDays = root.projectCleanupOptions[index].value
            }
        }

        SettingsSection { title: qsTr("Countdown") }

        SettingsToggle {
            resetKey: "countdownEnabled"
            label: qsTr("Show countdown")
            checked: settingsBackend.countdownEnabled
            onToggled: function(checked) { settingsBackend.countdownEnabled = checked }
        }

        SettingsCombo {
            resetKey: "countdownSeconds"
            label: qsTr("Duration")
            model: [
                { text: qsTr("1 second"), value: 1 },
                { text: qsTr("2 seconds"), value: 2 },
                { text: qsTr("3 seconds"), value: 3 },
                { text: qsTr("4 seconds"), value: 4 },
                { text: qsTr("5 seconds"), value: 5 }
            ]
            currentIndex: settingsBackend.countdownSeconds - 1
            enabled: settingsBackend.countdownEnabled
            onActivated: function(index) { settingsBackend.countdownSeconds = index + 1 }
        }
    }
}
