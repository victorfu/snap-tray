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
    readonly property bool audioCaptureSupported: settingsBackend.recordingShowPreview || settingsBackend.recordingOutputFormat === 0
    readonly property bool audioSourceUsesInputDevice: settingsBackend.recordingAudioSource !== 1
    readonly property var audioDeviceOptions: buildAudioDeviceOptions()

    function frameRateToIndex(fps) {
        for (var i = 0; i < frameRates.length; i++) {
            if (frameRates[i] === fps) return i
        }
        return 3 // default to 30
    }

    function buildAudioDeviceOptions() {
        var options = [{ text: qsTr("System Default"), value: "" }]
        var devices = settingsBackend.audioDevices()
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

        SettingsCombo {
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
            label: qsTr("Default output format")
            model: [
                { text: qsTr("MP4 (H.264)"), value: 0 },
                { text: qsTr("GIF"), value: 1 },
                { text: qsTr("WebP"), value: 2 }
            ]
            currentIndex: settingsBackend.recordingOutputFormat
            onActivated: function(index) { settingsBackend.recordingOutputFormat = index }
        }

        SettingsSlider {
            label: qsTr("Recording quality")
            value: settingsBackend.recordingQuality
            from: 0
            to: 100
            visible: settingsBackend.recordingShowPreview || settingsBackend.recordingOutputFormat === 0
            onMoved: settingsBackend.recordingQuality = value
        }

        Rectangle {
            width: parent.width - 2 * ComponentTokens.settingsContentPadding
            height: formatInfoTextItem.implicitHeight + 20
            radius: PrimitiveTokens.radiusSmall
            color: ComponentTokens.infoPanelBg
            border.width: 1
            border.color: ComponentTokens.infoPanelBorder
            visible: settingsBackend.recordingShowPreview || settingsBackend.recordingOutputFormat > 0

            Text {
                id: formatInfoTextItem
                anchors.fill: parent
                anchors.margins: 10
                text: root.formatInfoText()
                color: ComponentTokens.infoPanelText
                font.pixelSize: PrimitiveTokens.fontSizeBody
                font.family: PrimitiveTokens.fontFamily
                font.letterSpacing: PrimitiveTokens.letterSpacingTight
                wrapMode: Text.WordWrap
            }
        }

        SettingsSection { title: qsTr("Audio") }

        SettingsToggle {
            label: qsTr("Record audio")
            description: root.audioCaptureSupported
                ? (settingsBackend.recordingShowPreview
                    ? qsTr("Audio is captured in the temporary MP4 recording. Only MP4 export keeps audio.")
                    : "")
                : qsTr("Audio is only available for direct MP4 recording, or when Show preview is enabled.")
            checked: settingsBackend.recordingAudioEnabled
            enabled: root.audioCaptureSupported
            opacity: enabled ? 1.0 : 0.6
            onToggled: settingsBackend.recordingAudioEnabled = checked
        }

        SettingsCombo {
            label: qsTr("Source")
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

        SettingsCombo {
            label: qsTr("Input device")
            model: root.audioDeviceOptions
            currentIndex: root.audioDeviceToIndex(settingsBackend.recordingAudioDevice)
            visible: root.audioCaptureSupported && root.audioSourceUsesInputDevice
            enabled: settingsBackend.recordingAudioEnabled
            opacity: enabled ? 1.0 : 0.6
            onActivated: function(index) {
                settingsBackend.recordingAudioDevice = root.audioDeviceOptions[index].value
            }
        }

        Spacer {}

        SettingsToggle {
            label: qsTr("Show preview")
            description: qsTr("Keep this on to trim or export after recording. Preview recordings are always captured as MP4 first.")
            checked: settingsBackend.recordingShowPreview
            onToggled: settingsBackend.recordingShowPreview = checked
        }

        SettingsSection { title: qsTr("Countdown") }

        SettingsToggle {
            label: qsTr("Show countdown")
            checked: settingsBackend.countdownEnabled
            onToggled: settingsBackend.countdownEnabled = checked
        }

        SettingsCombo {
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
